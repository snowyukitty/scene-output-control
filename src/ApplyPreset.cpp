// SPDX-License-Identifier: GPL-2.0-or-later

#include "ApplyPreset.hpp"
#include "AudioSessionState.hpp"
#include "PresetValidation.hpp"
#include "plugin-log.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>
#include <util/platform.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#endif

#include <algorithm>
#include <cwchar>
#include <cstring>
#include <string>
#include <utility>
#ifdef _WIN32
#include <vector>
#endif

// ---------------------------------------------------------------------------
// Module-local state
// ---------------------------------------------------------------------------

enum class RecordingRestartState {
	Idle,
	Stopping,
	Starting,
};

static RecordingRestartState g_recording_restart_state = RecordingRestartState::Idle;
static bool g_restart_was_paused = false;
static bool g_restart_start_acknowledged = false;
static VideoApplyResult g_restart_video_result = VideoApplyResult::NoChange;

// "Mute to me" temporarily suppresses only OBS's per-source monitoring path.
// A source using Monitor and Output is changed to Monitor Off, which preserves
// its normal output/encoder path. A Monitor Only source is temporarily muted,
// because changing it to Monitor Off would incorrectly add it to the output
// mix. Exact recovery metadata lives in each source's private settings, so an
// OBS save or interrupted run cannot permanently lose the user's setup.
enum class MuteBackend {
	None,
	SourceMonitoring,
};

static bool g_muted_to_me = false;
static MuteBackend g_mute_backend = MuteBackend::None;
static bool g_monitoring_mute_waiting = false;
static bool g_monitoring_mute_degraded = false;
static bool g_monitoring_mute_error_reported = false;
static bool g_source_restore_pending = false;
static constexpr const char *kMonitoringMuteRestoreKey = "aro_mute_to_me_restore";

// Kept only to recover configurations written by plugin versions that routed
// monitoring to an invalid sentinel device.
static constexpr const char *kSilentMonitorId = "aro::muted-to-you::silent";

static StatusReporter g_status_reporter;

#ifdef _WIN32
static std::vector<AudioSessionMuteSnapshot> g_saved_audio_session_mutes;
static bool g_windows_recovery_loaded = false;
static int g_windows_recovery_retry_countdown = 0;
static HANDLE g_windows_mute_instance_guard = nullptr;
static bool g_windows_mute_instance_guard_checked = false;
static bool g_windows_mute_blocked_by_other_instance = false;
static constexpr int kWindowsRecoveryRetryTicks = 10;
static constexpr const char *kWindowsMuteRecoveryFile = "mute-recovery.json";
static constexpr const wchar_t *kWindowsMuteInstanceGuardName = L"Local\\obs-auto-resize-output-mute-recovery-v1";
#endif

void aro_set_status_reporter(StatusReporter reporter)
{
	g_status_reporter = std::move(reporter);
}

static void report(const std::string &msg)
{
	if (g_status_reporter)
		g_status_reporter(msg);
}

struct MonitoringMuteResult {
	bool ok = true;
	int matched_sources = 0;
	int changed_sources = 0;
	int pending_sources = 0;
	bool persistent_state_changed = false;
};

static bool suppress_source_monitoring(void *param, obs_source_t *source)
{
	auto &result = *static_cast<MonitoringMuteResult *>(param);
	obs_data_t *private_settings = obs_source_get_private_settings(source);
	if (!private_settings) {
		result.ok = false;
		return true;
	}

	obs_data_t *snapshot = obs_data_get_obj(private_settings, kMonitoringMuteRestoreKey);
	enum obs_monitoring_type original_type;

	if (snapshot) {
		const long long stored_type = obs_data_get_int(snapshot, "monitoring_type");
		if (stored_type < OBS_MONITORING_TYPE_NONE || stored_type > OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT) {
			ARO_LOG(LOG_WARNING, "Ignoring invalid per-source mute recovery state");
			obs_data_unset_user_value(private_settings, kMonitoringMuteRestoreKey);
			obs_data_release(snapshot);
			obs_data_release(private_settings);
			result.ok = false;
			result.persistent_state_changed = true;
			return true;
		}

		original_type = static_cast<enum obs_monitoring_type>(stored_type);
	} else {
		original_type = obs_source_get_monitoring_type(source);
		if (original_type == OBS_MONITORING_TYPE_NONE) {
			obs_data_release(private_settings);
			return true;
		}

		const bool original_muted = obs_source_muted(source);
		const bool forced_mute = original_type == OBS_MONITORING_TYPE_MONITOR_ONLY && !original_muted;
		snapshot = obs_data_create();
		obs_data_set_int(snapshot, "schema_version", 1);
		obs_data_set_int(snapshot, "monitoring_type", original_type);
		obs_data_set_bool(snapshot, "muted", original_muted);
		obs_data_set_bool(snapshot, "forced_mute", forced_mute);
		obs_data_set_obj(private_settings, kMonitoringMuteRestoreKey, snapshot);
		result.persistent_state_changed = true;
	}

	++result.matched_sources;
	bool changed = false;
	if (original_type == OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT) {
		if (obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_NONE) {
			obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_NONE);
			changed = true;
		}
		if (obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_NONE)
			result.ok = false;
	} else if (original_type == OBS_MONITORING_TYPE_MONITOR_ONLY) {
		if (obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_MONITOR_ONLY) {
			obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_ONLY);
			changed = true;
		}
		if (!obs_source_muted(source)) {
			obs_source_set_muted(source, true);
			changed = true;
		}
		if (obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_MONITOR_ONLY ||
		    !obs_source_muted(source))
			result.ok = false;
	}

	if (changed)
		++result.changed_sources;
	obs_data_release(snapshot);
	obs_data_release(private_settings);
	return true;
}

static MonitoringMuteResult suppress_monitored_sources()
{
	MonitoringMuteResult result;
	obs_enum_sources(suppress_source_monitoring, &result);
	if (result.persistent_state_changed)
		obs_frontend_save();
	return result;
}

static bool restore_source_monitoring(void *param, obs_source_t *source)
{
	auto &result = *static_cast<MonitoringMuteResult *>(param);
	obs_data_t *private_settings = obs_source_get_private_settings(source);
	if (!private_settings) {
		result.ok = false;
		return true;
	}

	obs_data_t *snapshot = obs_data_get_obj(private_settings, kMonitoringMuteRestoreKey);
	if (!snapshot) {
		obs_data_release(private_settings);
		return true;
	}

	const long long stored_type = obs_data_get_int(snapshot, "monitoring_type");
	const bool original_muted = obs_data_get_bool(snapshot, "muted");
	const bool forced_mute = obs_data_get_bool(snapshot, "forced_mute");
	++result.matched_sources;
	if (stored_type >= OBS_MONITORING_TYPE_NONE && stored_type <= OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT) {
		const auto original_type = static_cast<enum obs_monitoring_type>(stored_type);
		if (obs_source_get_monitoring_type(source) != original_type)
			obs_source_set_monitoring_type(source, original_type);
		if (forced_mute && obs_source_muted(source) != original_muted)
			obs_source_set_muted(source, original_muted);
		if (obs_source_get_monitoring_type(source) != original_type ||
		    (forced_mute && obs_source_muted(source) != original_muted)) {
			result.ok = false;
			++result.pending_sources;
		} else {
			++result.changed_sources;
			obs_data_unset_user_value(private_settings, kMonitoringMuteRestoreKey);
			result.persistent_state_changed = true;
		}
	} else {
		result.ok = false;
		ARO_LOG(LOG_WARNING, "Discarding invalid per-source mute recovery state");
		obs_data_unset_user_value(private_settings, kMonitoringMuteRestoreKey);
		result.persistent_state_changed = true;
	}
	obs_data_release(snapshot);
	obs_data_release(private_settings);
	return true;
}

static MonitoringMuteResult restore_monitored_sources(bool save = true)
{
	MonitoringMuteResult result;
	obs_enum_sources(restore_source_monitoring, &result);
	if (save && result.persistent_state_changed)
		obs_frontend_save();
	return result;
}

#ifdef _WIN32
template<typename T> static void release_com(T *&ptr)
{
	if (ptr) {
		ptr->Release();
		ptr = nullptr;
	}
}

class ScopedComInit {
public:
	ScopedComInit()
	{
		m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		m_uninit = SUCCEEDED(m_hr);
	}

	~ScopedComInit()
	{
		if (m_uninit)
			CoUninitialize();
	}

	bool ok() const { return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE; }

	HRESULT result() const { return m_hr; }

private:
	HRESULT m_hr = S_OK;
	bool m_uninit = false;
};

static bool ensure_windows_mute_instance_guard()
{
	if (g_windows_mute_instance_guard)
		return true;
	if (g_windows_mute_instance_guard_checked)
		return false;

	g_windows_mute_instance_guard_checked = true;
	SetLastError(ERROR_SUCCESS);
	HANDLE guard = CreateMutexW(nullptr, FALSE, kWindowsMuteInstanceGuardName);
	if (!guard) {
		ARO_LOG(LOG_WARNING, "Failed to create the Windows mute-to-me instance guard: 0x%08lX",
			(unsigned long)GetLastError());
		return false;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(guard);
		g_windows_mute_blocked_by_other_instance = true;
		ARO_LOG(LOG_WARNING, "Mute-to-me disabled because another OBS instance owns its recovery state");
		return false;
	}

	g_windows_mute_instance_guard = guard;
	return true;
}

static AudioSessionMuteSnapshot *find_saved_audio_session_mute(const std::string &endpoint_id,
							       const std::string &session_id,
							       const std::string &instance_id)
{
	const size_t index =
		find_audio_session_snapshot(g_saved_audio_session_mutes, endpoint_id, session_id, instance_id);
	return index == kAudioSessionSnapshotNotFound ? nullptr : &g_saved_audio_session_mutes[index];
}

static std::string wide_to_utf8(const wchar_t *text)
{
	if (!text || !*text)
		return {};

	const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1)
		return {};

	std::string out((size_t)len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
	out.resize((size_t)len - 1);
	return out;
}

static std::string windows_mute_recovery_path()
{
	char *raw_path = obs_module_config_path(kWindowsMuteRecoveryFile);
	if (!raw_path)
		return {};

	std::string path = raw_path;
	bfree(raw_path);
	return path;
}

static bool remove_windows_mute_recovery_file(const std::string &path)
{
	if (!os_file_exists(path.c_str()))
		return true;
	if (os_unlink(path.c_str()) == 0)
		return true;

	ARO_LOG(LOG_WARNING, "Failed to remove stale mute recovery file '%s'", path.c_str());
	return false;
}

static bool clear_windows_mute_recovery_files()
{
	const std::string path = windows_mute_recovery_path();
	if (path.empty())
		return false;

	const std::string backup_path = path + ".bak";
	const std::string temporary_path = path + ".tmp";

	// Remove auxiliary copies first. If either is locked, retain the primary
	// file too so a future load cannot fall back to stale recovery metadata.
	const bool auxiliary_removed = remove_windows_mute_recovery_file(backup_path) &&
				       remove_windows_mute_recovery_file(temporary_path);
	return auxiliary_removed && remove_windows_mute_recovery_file(path);
}

static bool save_windows_mute_recovery_state()
{
	if (g_saved_audio_session_mutes.empty())
		return clear_windows_mute_recovery_files();

	const std::string path = windows_mute_recovery_path();
	const size_t separator = path.find_last_of("/\\");
	if (separator == std::string::npos)
		return false;
	const std::string directory = path.substr(0, separator);

	const int mkdir_result = os_mkdirs(directory.c_str());
	if (mkdir_result == MKDIR_ERROR) {
		ARO_LOG(LOG_WARNING, "Failed to create mute recovery directory");
		return false;
	}

	obs_data_t *root = obs_data_create();
	obs_data_array_t *sessions = obs_data_array_create();
	obs_data_set_int(root, "schema_version", 1);

	for (const AudioSessionMuteSnapshot &saved : g_saved_audio_session_mutes) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "endpoint_id", saved.endpoint_id.c_str());
		obs_data_set_string(item, "session_id", saved.session_id.c_str());
		obs_data_set_string(item, "instance_id", saved.instance_id.c_str());
		obs_data_set_bool(item, "muted", saved.muted);
		obs_data_array_push_back(sessions, item);
		obs_data_release(item);
	}

	obs_data_set_array(root, "sessions", sessions);
	const bool saved = obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	obs_data_array_release(sessions);
	obs_data_release(root);

	if (!saved)
		ARO_LOG(LOG_WARNING, "Failed to save Windows mute recovery state");
	return saved;
}

static void load_windows_mute_recovery_state()
{
	if (g_windows_recovery_loaded)
		return;
	g_windows_recovery_loaded = true;

	const std::string path = windows_mute_recovery_path();
	if (path.empty())
		return;

	obs_data_t *root = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!root)
		return;

	obs_data_array_t *sessions = obs_data_get_array(root, "sessions");
	const size_t count = sessions ? obs_data_array_count(sessions) : 0;
	for (size_t index = 0; index < count; ++index) {
		obs_data_t *item = obs_data_array_item(sessions, index);
		if (!item)
			continue;

		AudioSessionMuteSnapshot snapshot;
		snapshot.endpoint_id = obs_data_get_string(item, "endpoint_id");
		snapshot.session_id = obs_data_get_string(item, "session_id");
		snapshot.instance_id = obs_data_get_string(item, "instance_id");
		snapshot.muted = obs_data_get_bool(item, "muted");
		if (!snapshot.endpoint_id.empty() && (!snapshot.session_id.empty() || !snapshot.instance_id.empty())) {
			g_saved_audio_session_mutes.push_back(std::move(snapshot));
		}
		obs_data_release(item);
	}

	if (sessions)
		obs_data_array_release(sessions);
	obs_data_release(root);

	if (g_saved_audio_session_mutes.empty()) {
		if (!clear_windows_mute_recovery_files())
			ARO_LOG(LOG_WARNING, "Could not clear empty Windows mute recovery state");
	} else {
		ARO_LOG(LOG_WARNING, "Found %zu interrupted Windows mute session snapshot(s); recovery pending",
			g_saved_audio_session_mutes.size());
	}
}

static std::wstring device_id_for_device(IMMDevice *device)
{
	if (!device)
		return {};

	LPWSTR raw_id = nullptr;
	std::wstring id;
	const HRESULT hr = device->GetId(&raw_id);
	if (SUCCEEDED(hr) && raw_id)
		id = raw_id;
	if (raw_id)
		CoTaskMemFree(raw_id);
	return id;
}

template<typename Func>
static bool for_each_obs_audio_session(Func &&func, int &matched_sessions, const std::wstring &target_endpoint_id)
{
	matched_sessions = 0;

	ScopedComInit com;
	if (!com.ok()) {
		ARO_LOG(LOG_WARNING, "CoInitializeEx failed while muting OBS audio sessions: 0x%08lX",
			(unsigned long)com.result());
		return false;
	}

	IMMDeviceEnumerator *device_enum = nullptr;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enum));
	if (FAILED(hr)) {
		ARO_LOG(LOG_WARNING, "Failed to create IMMDeviceEnumerator: 0x%08lX", (unsigned long)hr);
		return false;
	}

	IMMDeviceCollection *devices = nullptr;
	hr = device_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
	if (FAILED(hr)) {
		ARO_LOG(LOG_WARNING, "Failed to enumerate render endpoints: 0x%08lX", (unsigned long)hr);
		release_com(device_enum);
		return false;
	}

	UINT device_count = 0;
	hr = devices->GetCount(&device_count);
	if (FAILED(hr)) {
		ARO_LOG(LOG_WARNING, "Failed to count render endpoints: 0x%08lX", (unsigned long)hr);
		release_com(devices);
		release_com(device_enum);
		return false;
	}

	const DWORD current_pid = GetCurrentProcessId();
	bool ok = true;

	for (UINT i = 0; i < device_count; ++i) {
		IMMDevice *device = nullptr;
		hr = devices->Item(i, &device);
		if (FAILED(hr))
			continue;

		const std::wstring endpoint_id = device_id_for_device(device);
		if (!target_endpoint_id.empty() &&
		    (endpoint_id.empty() || _wcsicmp(endpoint_id.c_str(), target_endpoint_id.c_str()) != 0)) {
			release_com(device);
			continue;
		}
		const std::string endpoint_id_utf8 = wide_to_utf8(endpoint_id.c_str());
		if (endpoint_id_utf8.empty()) {
			release_com(device);
			continue;
		}

		IAudioSessionManager2 *manager = nullptr;
		hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void **)&manager);
		release_com(device);
		if (FAILED(hr)) {
			if (!target_endpoint_id.empty()) {
				ARO_LOG(LOG_WARNING,
					"Failed to open the OBS monitoring endpoint session manager: 0x%08lX",
					(unsigned long)hr);
				ok = false;
			}
			continue;
		}

		IAudioSessionEnumerator *sessions = nullptr;
		hr = manager->GetSessionEnumerator(&sessions);
		release_com(manager);
		if (FAILED(hr)) {
			if (!target_endpoint_id.empty()) {
				ARO_LOG(LOG_WARNING,
					"Failed to enumerate the OBS monitoring endpoint sessions: 0x%08lX",
					(unsigned long)hr);
				ok = false;
			}
			continue;
		}

		int session_count = 0;
		hr = sessions->GetCount(&session_count);
		if (FAILED(hr)) {
			if (!target_endpoint_id.empty()) {
				ARO_LOG(LOG_WARNING, "Failed to count the OBS monitoring endpoint sessions: 0x%08lX",
					(unsigned long)hr);
				ok = false;
			}
			release_com(sessions);
			continue;
		}

		for (int j = 0; j < session_count; ++j) {
			IAudioSessionControl *control = nullptr;
			hr = sessions->GetSession(j, &control);
			if (FAILED(hr))
				continue;

			IAudioSessionControl2 *control2 = nullptr;
			hr = control->QueryInterface(IID_PPV_ARGS(&control2));
			release_com(control);
			if (FAILED(hr))
				continue;

			DWORD pid = 0;
			hr = control2->GetProcessId(&pid);
			if (FAILED(hr) || pid != current_pid) {
				release_com(control2);
				continue;
			}

			ISimpleAudioVolume *volume = nullptr;
			hr = control2->QueryInterface(IID_PPV_ARGS(&volume));
			if (FAILED(hr)) {
				release_com(control2);
				continue;
			}

			LPWSTR raw_session_id = nullptr;
			std::string session_id;
			hr = control2->GetSessionIdentifier(&raw_session_id);
			if (SUCCEEDED(hr) && raw_session_id)
				session_id = wide_to_utf8(raw_session_id);
			if (raw_session_id)
				CoTaskMemFree(raw_session_id);

			LPWSTR raw_instance_id = nullptr;
			std::string instance_id;
			hr = control2->GetSessionInstanceIdentifier(&raw_instance_id);
			if (SUCCEEDED(hr) && raw_instance_id)
				instance_id = wide_to_utf8(raw_instance_id);
			if (raw_instance_id)
				CoTaskMemFree(raw_instance_id);
			if (instance_id.empty())
				instance_id = "obs-process-" + std::to_string(current_pid) + "-session-" +
					      std::to_string(matched_sessions);

			++matched_sessions;
			if (!func(volume, endpoint_id_utf8, session_id, instance_id))
				ok = false;

			release_com(volume);
			release_com(control2);
		}

		release_com(sessions);
	}

	release_com(devices);
	release_com(device_enum);
	return ok;
}

struct WindowsRestoreResult {
	bool ok = false;
	int restored_sessions = 0;
	size_t remaining_snapshots = 0;
};

static WindowsRestoreResult restore_windows_obs_audio_sessions()
{
	WindowsRestoreResult result;
	if (g_saved_audio_session_mutes.empty()) {
		result.ok = save_windows_mute_recovery_state();
		return result;
	}

	for (AudioSessionMuteSnapshot &saved : g_saved_audio_session_mutes)
		saved.restored = false;

	int matched_sessions = 0;
	result.ok = for_each_obs_audio_session(
		[&](ISimpleAudioVolume *volume, const std::string &endpoint_id, const std::string &session_id,
		    const std::string &instance_id) {
			AudioSessionMuteSnapshot *saved =
				find_saved_audio_session_mute(endpoint_id, session_id, instance_id);
			if (!saved)
				return true;

			BOOL currently_muted = FALSE;
			HRESULT hr = volume->GetMute(&currently_muted);
			if (FAILED(hr)) {
				ARO_LOG(LOG_WARNING,
					"Failed to read OBS audio session mute state during recovery: 0x%08lX",
					(unsigned long)hr);
				return false;
			}

			const BOOL restore_mute = saved->muted ? TRUE : FALSE;
			if (currently_muted != restore_mute) {
				hr = volume->SetMute(restore_mute, nullptr);
				if (FAILED(hr)) {
					ARO_LOG(LOG_WARNING, "Failed to restore OBS audio session mute state: 0x%08lX",
						(unsigned long)hr);
					return false;
				}
			}

			if (!saved->restored)
				++result.restored_sessions;
			saved->restored = true;
			return true;
		},
		matched_sessions, std::wstring());

	const std::vector<AudioSessionMuteSnapshot> snapshots_before_save = g_saved_audio_session_mutes;
	g_saved_audio_session_mutes.erase(
		std::remove_if(g_saved_audio_session_mutes.begin(), g_saved_audio_session_mutes.end(),
			       [](const AudioSessionMuteSnapshot &saved) { return saved.restored; }),
		g_saved_audio_session_mutes.end());
	result.remaining_snapshots = g_saved_audio_session_mutes.size();
	if (!save_windows_mute_recovery_state()) {
		g_saved_audio_session_mutes = snapshots_before_save;
		for (AudioSessionMuteSnapshot &saved : g_saved_audio_session_mutes)
			saved.restored = false;
		result.remaining_snapshots = g_saved_audio_session_mutes.size();
		result.ok = false;
	}

	return result;
}
#endif

// ---------------------------------------------------------------------------
// Recording config (profile config_t)
// ---------------------------------------------------------------------------

static bool config_string_is(config_t *cfg, const char *section, const char *key, const std::string &value)
{
	const char *current = config_get_string(cfg, section, key);
	return current && value == current;
}

static bool set_config_string_if_changed(config_t *cfg, const char *section, const char *key, const std::string &value)
{
	if (config_string_is(cfg, section, key, value))
		return false;
	config_set_string(cfg, section, key, value.c_str());
	return true;
}

static bool set_config_int_if_changed(config_t *cfg, const char *section, const char *key, int64_t value)
{
	if (config_get_int(cfg, section, key) == value)
		return false;
	config_set_int(cfg, section, key, value);
	return true;
}

static bool set_config_uint_if_changed(config_t *cfg, const char *section, const char *key, uint64_t value)
{
	if (config_get_uint(cfg, section, key) == value)
		return false;
	config_set_uint(cfg, section, key, value);
	return true;
}

static bool save_profile_config(config_t *cfg, const char *description)
{
	const int result = config_save_safe(cfg, "tmp", nullptr);
	if (result == 0)
		return true;

	ARO_LOG(LOG_WARNING, "Failed to save %s (config error %d)", description, result);
	report(std::string("Could not save ") + description + " (see log).");
	return false;
}

// OBS stores recording settings in different config sections depending on the
// chosen output mode. These keys target OBS 30+/31+ (RecFormat2 era).
static void apply_recording_config(const ScenePreset &p)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	const char *mode = config_get_string(cfg, "Output", "Mode");
	const bool advanced = mode && strcmp(mode, "Advanced") == 0;
	const char *section = advanced ? "AdvOut" : "SimpleOutput";

	bool changed = false;

	if (p.use_rec_path) {
		if (p.rec_path.empty()) {
			ARO_LOG(LOG_WARNING, "Recording path override is enabled but empty; ignored");
		} else {
			const char *key = advanced ? "RecFilePath" : "FilePath";
			changed = set_config_string_if_changed(cfg, section, key, p.rec_path) || changed;
		}
	}

	std::string effective_format;
	const char *current_format = config_get_string(cfg, section, "RecFormat2");
	if (current_format)
		effective_format = current_format;

	if (p.use_rec_format) {
		if (p.rec_format.empty()) {
			ARO_LOG(LOG_WARNING, "Recording format override is enabled but empty; ignored");
		} else if (!advanced && p.rec_format == "hls") {
			ARO_LOG(LOG_WARNING,
				"HLS recording format requires Advanced output mode; ignored in Simple mode");
			report("HLS recording format requires Advanced output mode; format override ignored.");
		} else {
			effective_format = p.rec_format;
			changed = set_config_string_if_changed(cfg, section, "RecFormat2", p.rec_format) || changed;
		}
	}

	if (p.use_audio_tracks) {
		// Bitmask of enabled recording tracks (bit0 = track 1).
		const uint32_t tracks = p.audio_tracks & kRecordingTrackMask;
		changed = set_config_int_if_changed(cfg, section, "RecTracks", tracks) || changed;

		if (effective_format == "flv") {
			if (advanced) {
				// Advanced FLV uses one one-based track index and ignores
				// RecTracks. Keep both keys current so changing containers
				// later preserves the full multi-track selection.
				uint32_t flv_track = first_selected_audio_track(tracks);
				if (flv_track == 0) {
					flv_track = 1;
					ARO_LOG(LOG_WARNING, "FLV requires one audio track; defaulting to track 1");
					report("FLV requires one audio track; using track 1.");
				} else if ((tracks & (tracks - 1u)) != 0) {
					ARO_LOG(LOG_WARNING, "FLV supports one audio track; using selected track %u",
						flv_track);
					report("FLV supports one audio track; using the first selected track.");
				}
				changed = set_config_int_if_changed(cfg, section, "FLVTrack", flv_track) || changed;
			} else if (tracks != 1u) {
				// SimpleOutput creates its FLV encoder on mixer 0 and does
				// not consult RecTracks.
				ARO_LOG(LOG_WARNING, "Simple-mode FLV always records audio track 1");
				report("Simple-mode FLV always records audio track 1; track selection was saved for other formats.");
			}
		}
	}

	if (changed) {
		if (save_profile_config(cfg, "recording profile settings")) {
			ARO_LOG(LOG_INFO, "Applied recording config (mode=%s) for upcoming recordings",
				advanced ? "Advanced" : "Simple");
		}
	}
}

// Recording video bitrate. In Advanced output mode the recording encoder's
// settings live in <profile>/recordEncoder.json; OBS reads that file when it
// builds the recording output at "Start Recording". We set a CBR bitrate there
// so it applies to the next recording. Simple mode has no separate recording
// bitrate (it shares the stream encoder), so we skip it rather than mutate
// streaming settings behind the user's back.
static void apply_recording_bitrate(const ScenePreset &p)
{
	if (!p.use_rec_bitrate)
		return;

	config_t *cfg = obs_frontend_get_profile_config();
	const char *mode = cfg ? config_get_string(cfg, "Output", "Mode") : nullptr;
	const bool advanced = mode && strcmp(mode, "Advanced") == 0;
	if (!advanced) {
		ARO_LOG(LOG_WARNING, "Recording bitrate override needs Advanced output mode; ignored in Simple mode");
		return;
	}

	const char *recording_type = config_get_string(cfg, "AdvOut", "RecType");
	if (recording_type && strcmp(recording_type, "FFmpeg") == 0) {
		ARO_LOG(LOG_WARNING, "Recording bitrate override is unavailable for Custom Output (FFmpeg)");
		return;
	}

	const char *recording_encoder = config_get_string(cfg, "AdvOut", "RecEncoder");
	if (!recording_encoder || !*recording_encoder || strcmp(recording_encoder, "none") == 0) {
		ARO_LOG(LOG_WARNING,
			"Recording bitrate override needs a dedicated recording encoder; ignored while using the stream encoder");
		return;
	}

	char *profile_path = obs_frontend_get_current_profile_path();
	if (!profile_path)
		return;
	const std::string path = std::string(profile_path) + "/recordEncoder.json";
	bfree(profile_path);

	// Preserve any existing encoder settings; only override rate control.
	obs_data_t *enc = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!enc)
		enc = obs_data_create();

	const char *rate_control = obs_data_get_string(enc, "rate_control");
	const bool already_cbr = rate_control && strcmp(rate_control, "CBR") == 0;
	const bool bitrate_matches = obs_data_get_int(enc, "bitrate") == p.rec_bitrate;
	if (already_cbr && bitrate_matches) {
		obs_data_release(enc);
		return;
	}

	obs_data_set_string(enc, "rate_control", "CBR");
	obs_data_set_int(enc, "bitrate", p.rec_bitrate);
	const bool saved = obs_data_save_json_safe(enc, path.c_str(), "tmp", "bak");
	obs_data_release(enc);

	if (!saved) {
		ARO_LOG(LOG_WARNING, "Failed to save recording encoder bitrate settings");
		report("Could not save recording encoder settings (see log).");
		return;
	}

	ARO_LOG(LOG_INFO, "Set recording encoder to CBR %u kbps for upcoming recordings", p.rec_bitrate);
}

// ---------------------------------------------------------------------------
// Audio monitoring device (the device YOU hear)
// ---------------------------------------------------------------------------

// Switch OBS's global audio monitoring device. Monitoring is purely a playback
// path: it feeds audio to a device for you to listen to and is completely
// separate from the encoder/output path that recording and streaming use.
// Re-pointing it therefore takes effect immediately, never interrupts an active
// recording, and never changes what is recorded or at what volume. This lets a
// scene send monitored audio to a device you are not listening to without
// changing the source's existing OBS output/recording routing.
static void apply_monitoring_device(const ScenePreset &p)
{
	if (!p.use_monitor_device)
		return;

	if (!obs_audio_monitoring_available()) {
		ARO_LOG(LOG_WARNING, "Audio monitoring is not available on this system; monitor device ignored");
		return;
	}
	if (p.monitor_device_name.empty() || p.monitor_device_id.empty()) {
		ARO_LOG(LOG_WARNING, "Audio monitoring device override is incomplete; ignored");
		return;
	}

	const char *current_id = nullptr;
	obs_get_audio_monitoring_device(nullptr, &current_id);
	if (current_id && p.monitor_device_id == current_id) {
		if (g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring)
			aro_maintain_mute_to_me();
		return;
	}

	if (obs_set_audio_monitoring_device(p.monitor_device_name.c_str(), p.monitor_device_id.c_str())) {
		ARO_LOG(LOG_INFO, "Set audio monitoring device to '%s'",
			p.monitor_device_name.empty() ? p.monitor_device_id.c_str() : p.monitor_device_name.c_str());
		if (g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring)
			aro_maintain_mute_to_me();
	} else {
		ARO_LOG(LOG_WARNING, "Failed to set audio monitoring device to '%s' (id '%s')",
			p.monitor_device_name.c_str(), p.monitor_device_id.c_str());
	}
}

// ---------------------------------------------------------------------------
// Video (obs_reset_video)
// ---------------------------------------------------------------------------

// Mirror applied video values into the profile config so the Settings dialog
// and the next launch reflect what is actually running.
static void persist_video_config(const ScenePreset &p, const struct obs_video_info &ovi)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	bool changed = false;
	if (p.use_base_res) {
		changed = set_config_uint_if_changed(cfg, "Video", "BaseCX", ovi.base_width) || changed;
		changed = set_config_uint_if_changed(cfg, "Video", "BaseCY", ovi.base_height) || changed;
	}
	if (p.use_output_res) {
		changed = set_config_uint_if_changed(cfg, "Video", "OutputCX", ovi.output_width) || changed;
		changed = set_config_uint_if_changed(cfg, "Video", "OutputCY", ovi.output_height) || changed;
	}
	if (p.use_fps) {
		// FPSType 1 == "Integer FPS Value"; FPSInt holds the value.
		changed = set_config_uint_if_changed(cfg, "Video", "FPSType", 1) || changed;
		changed = set_config_uint_if_changed(cfg, "Video", "FPSInt", p.fps) || changed;
	}
	if (changed)
		save_profile_config(cfg, "video profile settings");
}

static VideoApplyResult apply_video(const ScenePreset &p)
{
	ScenePreset normalized = p;
	normalize_scene_preset(normalized);
	if (!normalized.any_video_override())
		return VideoApplyResult::NoChange;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		// Video pipeline not initialized yet.
		return VideoApplyResult::Failed;
	}

	const VideoState current = {ovi.base_width,    ovi.base_height, ovi.output_width,
				    ovi.output_height, ovi.fps_num,     ovi.fps_den};
	if (video_override_matches(normalized, current)) {
		// Keep the profile's Settings UI in sync without tearing down and
		// rebuilding an already-correct video pipeline.
		persist_video_config(normalized, ovi);
		ARO_LOG(LOG_DEBUG, "Video override already matches the live pipeline; reset skipped");
		return VideoApplyResult::NoChange;
	}

	// obs_get_video_info copies the stored struct, which normally carries the
	// graphics_module pointer from first init. Guard against it being unset so
	// obs_reset_video can always (re)initialize graphics if it ever needs to.
	if (!ovi.graphics_module) {
#ifdef _WIN32
		ovi.graphics_module = "libobs-d3d11";
#else
		ovi.graphics_module = "libobs-opengl";
#endif
	}

	if (normalized.use_base_res) {
		ovi.base_width = normalized.base_cx;
		ovi.base_height = normalized.base_cy;
	}
	if (normalized.use_output_res) {
		ovi.output_width = normalized.output_cx;
		ovi.output_height = normalized.output_cy;
	}
	if (normalized.use_fps) {
		ovi.fps_num = normalized.fps;
		ovi.fps_den = 1;
	}

	const int r = obs_reset_video(&ovi);
	switch (r) {
	case OBS_VIDEO_SUCCESS:
		persist_video_config(normalized, ovi);
		ARO_LOG(LOG_INFO, "Applied video: base=%ux%u output=%ux%u fps=%u/%u", ovi.base_width, ovi.base_height,
			ovi.output_width, ovi.output_height, ovi.fps_num, ovi.fps_den);
		return VideoApplyResult::Applied;
	case OBS_VIDEO_CURRENTLY_ACTIVE:
		return VideoApplyResult::BlockedActive;
	default:
		ARO_LOG(LOG_WARNING, "obs_reset_video failed (code %d)", r);
		return VideoApplyResult::Failed;
	}
}

// ---------------------------------------------------------------------------
// "Mute to me" (global monitoring toggle)
// ---------------------------------------------------------------------------

bool aro_mute_to_me_active()
{
	return g_muted_to_me;
}

bool aro_mute_to_me_available()
{
	if (!obs_audio_monitoring_available())
		return false;
#ifdef _WIN32
	return ensure_windows_mute_instance_guard();
#else
	return true;
#endif
}

bool aro_mute_to_me_blocked_by_other_instance()
{
#ifdef _WIN32
	return g_windows_mute_blocked_by_other_instance;
#else
	return false;
#endif
}

bool aro_mute_to_me_waiting_for_source()
{
	return g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring && g_monitoring_mute_waiting;
}

bool aro_mute_to_me_degraded()
{
	return g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring && g_monitoring_mute_degraded;
}

bool aro_maintain_mute_to_me()
{
	if (g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring) {
		const bool was_waiting = g_monitoring_mute_waiting;
		const bool was_degraded = g_monitoring_mute_degraded;
		const MonitoringMuteResult result = suppress_monitored_sources();
		if (result.ok) {
			g_monitoring_mute_waiting = result.matched_sources == 0;
			g_monitoring_mute_degraded = false;
			if (result.changed_sources > 0) {
				ARO_LOG(LOG_INFO, "Mute-to-me suppressed monitoring on %d new or changed source(s)",
					result.changed_sources);
			}
			if (g_monitoring_mute_error_reported) {
				ARO_LOG(LOG_INFO, "Mute-to-me source monitoring maintenance recovered");
				g_monitoring_mute_error_reported = false;
			}
		} else {
			g_monitoring_mute_degraded = true;
			if (!g_monitoring_mute_error_reported) {
				ARO_LOG(LOG_WARNING,
					"Mute-to-me could not maintain one or more source monitoring states");
				report("Mute-to-me could not suppress one or more monitored sources; monitoring may be audible.");
				g_monitoring_mute_error_reported = true;
			}
		}
		return was_waiting != g_monitoring_mute_waiting || was_degraded != g_monitoring_mute_degraded;
	}

	if (g_source_restore_pending) {
		const MonitoringMuteResult result = restore_monitored_sources();
		g_source_restore_pending = result.pending_sources != 0;
		if (!g_source_restore_pending && result.changed_sources > 0)
			ARO_LOG(LOG_INFO, "Recovered monitoring state on %d source(s)", result.changed_sources);
	}

#ifdef _WIN32
	// Recover session mutes written by pre-fix development builds. New mute
	// operations never touch Windows audio-session volume because OBS process
	// loopback capture shares that session and would also be silenced.
	if (!g_saved_audio_session_mutes.empty()) {
		if (g_windows_recovery_retry_countdown > 0) {
			--g_windows_recovery_retry_countdown;
		} else {
			const WindowsRestoreResult result = restore_windows_obs_audio_sessions();
			g_windows_recovery_retry_countdown = kWindowsRecoveryRetryTicks;
			if (result.restored_sessions > 0) {
				ARO_LOG(LOG_INFO, "Recovered %d interrupted Windows audio session(s)",
					result.restored_sessions);
			}
		}
	}
#endif
	return false;
}

void aro_set_mute_to_me(bool mute)
{
	if (mute == g_muted_to_me)
		return;

	if (mute && !aro_mute_to_me_available()) {
		if (aro_mute_to_me_blocked_by_other_instance()) {
			report("Mute to me is owned by another OBS instance. Close it and restart this OBS instance.");
		} else {
			ARO_LOG(LOG_WARNING, "Audio monitoring not available; 'mute to me' has no effect");
			report("Mute to me is unavailable; see the OBS log.");
		}
		return;
	}

	if (mute) {
		const MonitoringMuteResult result = suppress_monitored_sources();
		if (!result.ok) {
			const MonitoringMuteResult restore_result = restore_monitored_sources();
			g_source_restore_pending = restore_result.pending_sources != 0;
			g_monitoring_mute_waiting = false;
			g_monitoring_mute_degraded = false;
			ARO_LOG(LOG_WARNING, "Mute-to-me could not arm source monitoring suppression");
			report("Could not mute monitored sources safely; the prior monitoring state was restored.");
			return;
		}

		g_mute_backend = MuteBackend::SourceMonitoring;
		g_muted_to_me = true;
		g_monitoring_mute_waiting = result.matched_sources == 0;
		g_monitoring_mute_error_reported = false;
		g_monitoring_mute_degraded = false;
		if (g_monitoring_mute_waiting) {
			ARO_LOG(LOG_INFO, "Mute-to-me ARMED: waiting for a monitored source");
			report("Mute armed: monitored audio will be silenced when OBS playback starts. Recording is unchanged.");
		} else {
			ARO_LOG(LOG_INFO, "Mute-to-me ON: suppressed monitoring on %d source(s); recording unaffected",
				result.matched_sources);
			report("Muted to you: OBS source monitoring is off. The recording mix is unchanged.");
		}
		return;
	} else {
		const MonitoringMuteResult result = restore_monitored_sources();
		g_source_restore_pending = result.pending_sources != 0;
		if (g_source_restore_pending) {
			ARO_LOG(LOG_WARNING, "Mute-to-me OFF with %d source recovery operation(s) still pending",
				result.pending_sources);
			report("Monitoring unmute requested; some source state recovery is still pending.");
		} else {
			report("Unmuted to you: monitoring restored.");
		}
		g_mute_backend = MuteBackend::None;
		g_muted_to_me = false;
		g_monitoring_mute_waiting = false;
		g_monitoring_mute_degraded = false;
		ARO_LOG(LOG_INFO, "Mute-to-me OFF: monitoring playback restored");
	}
}

void aro_recover_monitoring_on_load()
{
#ifdef _WIN32
	const bool owns_recovery_state = ensure_windows_mute_instance_guard();
#else
	const bool owns_recovery_state = true;
#endif
	if (owns_recovery_state) {
		const MonitoringMuteResult source_result = restore_monitored_sources();
		g_source_restore_pending = source_result.pending_sources != 0;
		if (source_result.changed_sources > 0) {
			ARO_LOG(LOG_INFO, "Startup recovered monitoring state on %d source(s)",
				source_result.changed_sources);
			report("Recovered source monitoring state left by an interrupted OBS run.");
		}
	}

#ifdef _WIN32
	if (owns_recovery_state) {
		load_windows_mute_recovery_state();
		if (!g_saved_audio_session_mutes.empty()) {
			const WindowsRestoreResult result = restore_windows_obs_audio_sessions();
			if (result.restored_sessions > 0) {
				ARO_LOG(LOG_INFO, "Startup recovered %d interrupted Windows audio session(s)",
					result.restored_sessions);
				report("Recovered Windows monitoring mute state left by an interrupted OBS run.");
			}
			if (result.remaining_snapshots != 0)
				g_windows_recovery_retry_countdown = 0;
		}
	}
#endif

	// If an older run left the sentinel device persisted, reset to Default.
	if (!obs_audio_monitoring_available())
		return;
	const char *id = nullptr;
	obs_get_audio_monitoring_device(nullptr, &id);
	if (id && strcmp(id, kSilentMonitorId) == 0) {
		ARO_LOG(LOG_INFO, "Recovered leftover 'muted to you' monitoring device -> Default");
		obs_set_audio_monitoring_device("Default", "default");
	}
}

void aro_on_scene_collection_changing()
{
	if (!g_muted_to_me || g_mute_backend != MuteBackend::SourceMonitoring)
		return;

	// The frontend is already in its collection-switch save path, so avoid a
	// re-entrant explicit save here. Recovery metadata is cleared before the
	// frontend serializes the old collection.
	const MonitoringMuteResult result = restore_monitored_sources(false);
	if (result.pending_sources != 0) {
		ARO_LOG(LOG_WARNING, "Scene collection change left %d source recovery operation(s) pending",
			result.pending_sources);
	}
}

void aro_on_scene_collection_changed()
{
#ifdef _WIN32
	if (!ensure_windows_mute_instance_guard())
		return;
#endif
	if (g_muted_to_me && g_mute_backend == MuteBackend::SourceMonitoring) {
		aro_maintain_mute_to_me();
		return;
	}

	const MonitoringMuteResult result = restore_monitored_sources();
	g_source_restore_pending = result.pending_sources != 0;
	if (result.changed_sources > 0)
		ARO_LOG(LOG_INFO, "Recovered monitoring state on %d source(s) after scene collection change",
			result.changed_sources);
}

// ---------------------------------------------------------------------------
// Top-level apply
// ---------------------------------------------------------------------------

void aro_apply_preset_for_scene(obs_source_t *scene)
{
	if (!scene)
		return;

	const ScenePreset p = preset_load(scene);
	if (!p.enabled)
		return;

	const char *scene_name = obs_source_get_name(scene);

	// Recording config can always be staged for the next recording.
	apply_recording_config(p);
	apply_recording_bitrate(p);

	// Monitoring device is independent of the output pipeline; apply it always
	// (it is safe even while recording/streaming).
	apply_monitoring_device(p);

	const VideoApplyResult vr = apply_video(p);

	switch (vr) {
	case VideoApplyResult::Applied:
		report(std::string("Applied output settings for scene '") + (scene_name ? scene_name : "") + "'.");
		break;

	case VideoApplyResult::BlockedActive:
		if (p.restart_recording && obs_frontend_recording_active() && !obs_frontend_streaming_active() &&
		    !obs_frontend_virtualcam_active() && !obs_frontend_replay_buffer_active()) {
			// Only a recording is holding the video pipeline; we can
			// stop it, change video, and start a fresh recording.
			if (g_recording_restart_state == RecordingRestartState::Idle) {
				ARO_LOG(LOG_INFO, "Scene '%s': restarting recording to apply video changes",
					scene_name ? scene_name : "");
				report("Stopping recording to apply new video settings...");

				g_recording_restart_state = RecordingRestartState::Stopping;
				g_restart_was_paused = obs_frontend_recording_paused();
				obs_frontend_recording_stop();
			} else {
				// A scene can change again while the recording is still
				// stopping. The stopped handler deliberately reads the
				// latest program scene, so no second stop is needed.
				report("Recording restart already in progress; the latest scene will be applied.");
			}
		} else {
			ARO_LOG(LOG_WARNING, "Scene '%s': cannot change resolution/FPS while an output is active",
				scene_name ? scene_name : "");
			report("Recording/streaming active: resolution & FPS "
			       "changes are deferred (OBS limitation). "
			       "Recording folder/format/tracks still apply to "
			       "the next recording.");
		}
		break;

	case VideoApplyResult::Failed:
		report("Failed to apply output settings (see log).");
		break;

	case VideoApplyResult::NoChange:
		break;
	}
}

void aro_on_recording_stopped()
{
	if (g_recording_restart_state == RecordingRestartState::Starting) {
		// A restarted output stopped before OBS reported it as started.
		g_recording_restart_state = RecordingRestartState::Idle;
		g_restart_was_paused = false;
		g_restart_start_acknowledged = false;
		g_restart_video_result = VideoApplyResult::NoChange;
		report("The recording restart failed or stopped immediately (see OBS log).");
		return;
	}

	if (g_recording_restart_state != RecordingRestartState::Stopping)
		return;

	g_recording_restart_state = RecordingRestartState::Starting;
	g_restart_start_acknowledged = false;
	g_restart_video_result = VideoApplyResult::NoChange;

	// The user may switch scenes again while OBS is stopping. Apply the latest
	// program scene rather than stale settings from the scene that initiated
	// the restart.
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (scene) {
		const ScenePreset p = preset_load(scene);
		if (p.enabled) {
			g_restart_video_result = apply_video(p);
			apply_recording_config(p);
			apply_recording_bitrate(p);
			apply_monitoring_device(p);
		}
		obs_source_release(scene);
	}

	report(g_restart_video_result == VideoApplyResult::Failed ||
			       g_restart_video_result == VideoApplyResult::BlockedActive
		       ? "Starting a new recording, but the requested video settings could not be applied."
		       : "Video settings are ready; starting a new recording...");

	// Begin a new recording with the applied settings. Report first because
	// some output implementations can emit RECORDING_STARTED synchronously.
	obs_frontend_recording_start();

	// OBS emits RECORDING_STARTING synchronously after its preflight checks.
	// If it did not, StartRecording returned early (for example, an invalid
	// path or low disk space) and no later STARTED/STOPPED event will arrive.
	if (g_recording_restart_state == RecordingRestartState::Starting && !g_restart_start_acknowledged) {
		g_recording_restart_state = RecordingRestartState::Idle;
		g_restart_video_result = VideoApplyResult::NoChange;
		g_restart_was_paused = false;
		report("Recording could not restart; check the recording path, disk space, and OBS log.");
	}
}

void aro_on_recording_starting()
{
	if (g_recording_restart_state == RecordingRestartState::Starting)
		g_restart_start_acknowledged = true;
}

void aro_on_recording_started()
{
	if (g_recording_restart_state != RecordingRestartState::Starting)
		return;

	const VideoApplyResult result = g_restart_video_result;
	const bool restore_pause = g_restart_was_paused;
	g_recording_restart_state = RecordingRestartState::Idle;
	g_restart_video_result = VideoApplyResult::NoChange;
	g_restart_was_paused = false;
	g_restart_start_acknowledged = false;

	if (restore_pause)
		obs_frontend_recording_pause(true);

	std::string message;
	switch (result) {
	case VideoApplyResult::Applied:
		message = "Recording restarted with the new output settings.";
		break;
	case VideoApplyResult::NoChange:
		message = "Recording restarted; video settings already matched the active scene.";
		break;
	case VideoApplyResult::BlockedActive:
	case VideoApplyResult::Failed:
		message = "Recording restarted, but the requested video settings could not be applied.";
		break;
	}
	if (restore_pause)
		message += " The paused state was restored.";
	report(message);
}

void aro_shutdown()
{
	g_recording_restart_state = RecordingRestartState::Idle;
	g_restart_video_result = VideoApplyResult::NoChange;
	g_restart_was_paused = false;
	g_restart_start_acknowledged = false;
	// Undo any "mute to me" state so monitored sources are not left changed by
	// this plugin. The per-source snapshots remain available for next-start
	// recovery if a source cannot be restored during shutdown.
	if (g_muted_to_me) {
		const MonitoringMuteResult result = restore_monitored_sources();
		g_source_restore_pending = result.pending_sources != 0;
		if (g_source_restore_pending)
			ARO_LOG(LOG_WARNING, "Shutdown left %d source monitoring recovery operation(s) pending",
				result.pending_sources);
		g_mute_backend = MuteBackend::None;
		g_muted_to_me = false;
	}

#ifdef _WIN32
	if (!g_saved_audio_session_mutes.empty())
		restore_windows_obs_audio_sessions();
	g_windows_recovery_retry_countdown = 0;
	if (g_windows_mute_instance_guard) {
		CloseHandle(g_windows_mute_instance_guard);
		g_windows_mute_instance_guard = nullptr;
	}
	g_windows_mute_instance_guard_checked = false;
	g_windows_mute_blocked_by_other_instance = false;
#endif
	g_monitoring_mute_waiting = false;
	g_monitoring_mute_degraded = false;
	g_monitoring_mute_error_reported = false;

	g_status_reporter = nullptr;
}
