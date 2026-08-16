// SPDX-License-Identifier: GPL-2.0-or-later
//
// ScenePreset: the per-scene recording/output override stored inside the scene
// source's private_settings. Because OBS copies a scene source's
// private_settings on duplication (see obs_scene_duplicate in libobs/obs-scene.c),
// presets are carried along automatically when the user duplicates a scene, and
// they are serialized into the scene-collection .json on save -- no extra
// persistence layer required.

#pragma once

#include <cstdint>
#include <string>

struct obs_source;
typedef struct obs_source obs_source_t;

// The nested object key under a scene source's private_settings.
inline constexpr const char *kPresetKey = "auto_resize_output";
inline constexpr uint32_t kPresetSchemaVersion = 1;

struct ScenePreset {
	// Master switch: when false this scene applies no overrides at all.
	bool enabled = false;

	// --- Video (applied via obs_reset_video; idle-only, see ApplyPreset) ---
	bool use_base_res = false;
	uint32_t base_cx = 1920;
	uint32_t base_cy = 1080;

	bool use_output_res = false;
	uint32_t output_cx = 1920;
	uint32_t output_cy = 1080;

	bool use_fps = false;
	uint32_t fps = 60; // integer FPS only in v1

	// --- Recording (written to the active profile config; takes effect at the
	//     next "Start Recording") ---
	bool use_rec_path = false;
	std::string rec_path;

	bool use_rec_format = false;
	// OBS "RecFormat2" identifier, e.g. "mkv", "hybrid_mp4", "mp4", "mov",
	// "fragmented_mp4", "fragmented_mov", "mpegts", "flv".
	std::string rec_format;

	bool use_audio_tracks = false;
	uint32_t audio_tracks = 1; // bitmask: bit0 = track 1 ... bit5 = track 6

	// Recording video bitrate in kbps. Robust only in Advanced output mode
	// (written to recordEncoder.json as CBR). Ignored in Simple mode.
	bool use_rec_bitrate = false;
	uint32_t rec_bitrate = 6000;

	// --- Audio monitoring (the device YOU hear; independent of recording) ---
	// Switches OBS's global audio monitoring device when this scene becomes
	// active. Monitoring is the playback path only -- it never touches the
	// encoders, so this applies instantly even mid-recording and does not change
	// the existing recording mix. Lets a scene route monitored audio to a
	// device you are not listening to without changing source/track routing.
	bool use_monitor_device = false;
	std::string monitor_device_name; // human-readable name (for obs_set + display)
	std::string monitor_device_id;   // device id (for obs_set + matching)

	// When a video override is requested but an output is active, stop the
	// current recording, apply the change, and start a new recording. Produces
	// a separate file -- OBS cannot change resolution mid-output.
	bool restart_recording = false;

	bool any_video_override() const { return enabled && (use_base_res || use_output_res || use_fps); }
	bool any_override() const
	{
		return enabled && (use_base_res || use_output_res || use_fps || use_rec_path || use_rec_format ||
				   use_audio_tracks || use_rec_bitrate || use_monitor_device);
	}
};

// Read/write a scene's preset from/to its private_settings. Safe to call with
// nullptr (load returns a default preset; save is a no-op).
ScenePreset preset_load(obs_source_t *scene);
void preset_save(obs_source_t *scene, const ScenePreset &preset);
