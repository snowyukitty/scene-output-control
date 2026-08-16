// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ScenePreset.hpp"

#include <cstdint>

inline constexpr uint32_t kMinVideoDimension = 2;
inline constexpr uint32_t kMinOutputWidth = 4;
inline constexpr uint32_t kMaxVideoDimension = 32 * 1024;
inline constexpr uint32_t kMaxPresetFps = 1000;
inline constexpr uint32_t kMinRecordingBitrate = 100;
inline constexpr uint32_t kMaxRecordingBitrate = 300000;
inline constexpr uint32_t kRecordingTrackMask = 0x3f;

struct VideoState {
	uint32_t base_width = 0;
	uint32_t base_height = 0;
	uint32_t output_width = 0;
	uint32_t output_height = 0;
	uint32_t fps_num = 0;
	uint32_t fps_den = 0;
};

// Bring persisted or UI-created values into the range accepted by libobs.
// Invalid overrides are disabled instead of being allowed to tear down the
// video pipeline with unusable parameters. Returns true when anything changed.
bool normalize_scene_preset(ScenePreset &preset);

// True when every requested video override is already represented by the live
// video state. FPS is compared as a rational value.
bool video_override_matches(const ScenePreset &preset, const VideoState &state);

// OBS stores AdvOut/FLVTrack as a one-based track number rather than the
// RecTracks bitmask used by the other recording containers.
uint32_t first_selected_audio_track(uint32_t track_mask);

// Convert OBS' rational FPS to the nearest integer for the integer-only preset
// editor. Returns zero when the denominator is invalid.
uint32_t rounded_integer_fps(uint32_t numerator, uint32_t denominator);
