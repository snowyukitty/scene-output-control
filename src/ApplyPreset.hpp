// SPDX-License-Identifier: GPL-2.0-or-later
//
// ApplyPreset: turns a ScenePreset into actual OBS state.
//
//  * Video (base/output resolution, FPS) -> obs_reset_video(). This only
//    succeeds while no output is active; libobs returns OBS_VIDEO_CURRENTLY_ACTIVE
//    otherwise. We mirror the applied values into the profile config so the
//    Settings UI and on-disk state stay consistent.
//
//  * Recording (folder, container format, audio tracks) -> the active profile
//    config_t. OBS reads these when you press "Start Recording", so we write
//    them on scene change and they apply to the next recording.

#pragma once

#include "ScenePreset.hpp"

#include <functional>
#include <string>

struct obs_source;
typedef struct obs_source obs_source_t;

enum class VideoApplyResult {
	NoChange,      // preset requested no video override
	Applied,       // obs_reset_video succeeded
	BlockedActive, // an output is active; cannot change video now
	Failed,        // obs_reset_video returned an unexpected error
};

// Optional UI status sink. The dock registers this to surface the result of
// the most recent automatic apply to the user.
using StatusReporter = std::function<void(const std::string &message)>;
void aro_set_status_reporter(StatusReporter reporter);

// Apply a single scene's preset (recording config always; video if idle).
// Handles the optional "restart recording to apply" path.
void aro_apply_preset_for_scene(obs_source_t *scene);

// Called from the frontend RECORDING_STOPPED handler to complete a pending
// stop -> apply -> start restart sequence.
void aro_on_recording_stopped();

// Called from RECORDING_STARTING to acknowledge that OBS accepted the restart
// request after validating its path, disk space, and output availability.
void aro_on_recording_starting();

// Called from RECORDING_STARTED to finish the restart state machine and
// restore a prior paused state.
void aro_on_recording_started();

// "Mute to me": a global, instant toggle that silences OBS-monitored audio
// without touching the recording mix. For Monitor and Output sources it
// temporarily disables only monitoring; Monitor Only sources are temporarily
// muted because they are already excluded from output. Exact recovery state is
// stored in source private settings before any change, persisted immediately,
// and restored on unmute, unload, or the next startup after interruption.
void aro_set_mute_to_me(bool mute);
bool aro_mute_to_me_active();
bool aro_mute_to_me_available();
bool aro_mute_to_me_blocked_by_other_instance();
bool aro_mute_to_me_waiting_for_source();
bool aro_mute_to_me_degraded();

// Re-assert an active source-monitoring mute, catch newly monitored sources,
// and retry interrupted recovery. Call periodically from the UI thread.
// Returns true when the user-visible armed/active state changed.
bool aro_maintain_mute_to_me();

// Defensive startup recovery. Restores persisted per-source monitoring state,
// legacy Windows audio-session snapshots, or an old silent-device sentinel.
void aro_recover_monitoring_on_load();

// Keep source-level mute recovery exact across scene-collection switches.
// Call before the old collection is saved/unloaded and after the new one loads.
void aro_on_scene_collection_changing();
void aro_on_scene_collection_changed();

// Release any pending state (call on module unload / exit). Also restores any
// source state changed by "mute to me".
void aro_shutdown();
