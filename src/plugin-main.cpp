// SPDX-License-Identifier: GPL-2.0-or-later
//
// Scene Output Control
// Per-scene output, recording, and monitoring presets for OBS Studio.
//
// On scene change the active scene's preset (stored in the scene source's
// private_settings) is applied: output/base resolution & FPS via
// obs_reset_video(), recording folder/format/audio-tracks via the profile
// config for the next recording, and audio monitoring changes via frontend
// monitoring APIs. Presets travel with a scene when it is duplicated, because
// libobs copies private_settings during duplication.

#include "PresetDock.hpp"
#include "ApplyPreset.hpp"
#include "plugin-log.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMetaObject>
#include <QTimer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-resize-output", "en-US")

namespace {

constexpr const char *kDockId = "auto_resize_output_dock";

PresetDock *g_dock = nullptr;
QTimer *g_mute_maintenance_timer = nullptr;

void apply_current_scene()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (scene) {
		aro_apply_preset_for_scene(scene);
		obs_source_release(scene);
	}
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// Clear any leftover "muted to you" device from a prior crash before
		// the startup scene's own monitor-device preset is applied.
		aro_recover_monitoring_on_load();
		// Apply the startup scene once the UI/video pipeline is ready.
		apply_current_scene();
		if (g_dock) {
			g_dock->refreshSceneList();
			g_dock->refreshMuteState();
		}
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		apply_current_scene();
		if (g_dock)
			g_dock->onProgramSceneChanged();
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		// Completes a pending "restart recording to apply" sequence.
		aro_on_recording_stopped();
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STARTING:
		aro_on_recording_starting();
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		aro_on_recording_started();
		break;

	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		// OBS rebuilds video/output state while changing profiles. Re-apply
		// the active scene after that reset and reload profile-scoped UI.
		apply_current_scene();
		if (g_dock)
			g_dock->onProfileChanged();
		break;

	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		if (g_dock)
			g_dock->refreshSceneList();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		aro_on_scene_collection_changing();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		aro_on_scene_collection_changed();
		apply_current_scene();
		if (g_dock) {
			g_dock->refreshSceneList();
			g_dock->refreshMuteState();
		}
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		// Frontend is going away; drop our event hook early.
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		break;

	default:
		break;
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Per-scene output, recording, and audio monitoring presets "
	       "(resolution, FPS, recording folder/format/tracks, mute-to-me).";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Scene Output Control";
}

bool obs_module_load(void)
{
	ARO_LOG(LOG_INFO, "loaded (version " PLUGIN_VERSION ")");

	// Create the dock. OBS reparents the widget into a dock and owns it
	// thereafter; obs_frontend_remove_dock() destroys it on unload.
	g_dock = new PresetDock();
	if (!obs_frontend_add_dock_by_id(kDockId, "Scene Output Control", g_dock)) {
		ARO_LOG(LOG_ERROR, "failed to create OBS dock (duplicate id '%s'?)", kDockId);
		delete g_dock;
		g_dock = nullptr;
		aro_shutdown();
		return false;
	}

	// Route apply-status messages to the dock (marshalled to the UI thread).
	aro_set_status_reporter([](const std::string &msg) {
		if (!g_dock)
			return;
		const QString qmsg = QString::fromStdString(msg);
		QMetaObject::invokeMethod(
			g_dock,
			[qmsg]() {
				if (g_dock)
					g_dock->showStatus(qmsg);
			},
			Qt::QueuedConnection);
	});

	g_mute_maintenance_timer = new QTimer(g_dock);
	g_mute_maintenance_timer->setInterval(500);
	g_mute_maintenance_timer->setTimerType(Qt::CoarseTimer);
	QObject::connect(g_mute_maintenance_timer, &QTimer::timeout, g_dock, []() {
		if (aro_maintain_mute_to_me() && g_dock)
			g_dock->refreshMuteState();
	});
	g_mute_maintenance_timer->start();

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (g_mute_maintenance_timer)
		g_mute_maintenance_timer->stop();
	aro_set_status_reporter(nullptr);
	aro_shutdown();

	obs_frontend_remove_dock(kDockId); // destroys the dock widget
	g_mute_maintenance_timer = nullptr;
	g_dock = nullptr;

	ARO_LOG(LOG_INFO, "unloaded");
}
