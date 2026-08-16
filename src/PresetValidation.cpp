// SPDX-License-Identifier: GPL-2.0-or-later

#include "PresetValidation.hpp"

namespace {

bool dimension_in_range(uint32_t value)
{
	return value >= kMinVideoDimension && value <= kMaxVideoDimension;
}

} // namespace

bool normalize_scene_preset(ScenePreset &preset)
{
	const ScenePreset defaults;
	bool changed = false;

	if (!dimension_in_range(preset.base_cx) || !dimension_in_range(preset.base_cy)) {
		changed = preset.use_base_res || preset.base_cx != defaults.base_cx ||
			  preset.base_cy != defaults.base_cy || changed;
		preset.use_base_res = false;
		preset.base_cx = defaults.base_cx;
		preset.base_cy = defaults.base_cy;
	}

	if (!dimension_in_range(preset.output_cx) || !dimension_in_range(preset.output_cy)) {
		changed = preset.use_output_res || preset.output_cx != defaults.output_cx ||
			  preset.output_cy != defaults.output_cy || changed;
		preset.use_output_res = false;
		preset.output_cx = defaults.output_cx;
		preset.output_cy = defaults.output_cy;
	} else {
		// obs_reset_video() rounds output width down to a multiple of four
		// and output height down to a multiple of two.
		const uint32_t normalized_width = preset.output_cx & ~3u;
		const uint32_t normalized_height = preset.output_cy & ~1u;
		if (normalized_width < kMinOutputWidth || normalized_height < kMinVideoDimension) {
			changed = true;
			preset.use_output_res = false;
			preset.output_cx = defaults.output_cx;
			preset.output_cy = defaults.output_cy;
		} else {
			changed = normalized_width != preset.output_cx || normalized_height != preset.output_cy ||
				  changed;
			preset.output_cx = normalized_width;
			preset.output_cy = normalized_height;
		}
	}

	if (preset.fps == 0 || preset.fps > kMaxPresetFps) {
		changed = preset.use_fps || preset.fps != defaults.fps || changed;
		preset.use_fps = false;
		preset.fps = defaults.fps;
	}

	const uint32_t normalized_tracks = preset.audio_tracks & kRecordingTrackMask;
	changed = normalized_tracks != preset.audio_tracks || changed;
	preset.audio_tracks = normalized_tracks;

	if (preset.rec_bitrate < kMinRecordingBitrate || preset.rec_bitrate > kMaxRecordingBitrate) {
		changed = preset.use_rec_bitrate || preset.rec_bitrate != defaults.rec_bitrate || changed;
		preset.use_rec_bitrate = false;
		preset.rec_bitrate = defaults.rec_bitrate;
	}

	return changed;
}

bool video_override_matches(const ScenePreset &preset, const VideoState &state)
{
	ScenePreset normalized = preset;
	normalize_scene_preset(normalized);

	if (!normalized.any_video_override())
		return true;

	if (normalized.use_base_res &&
	    (normalized.base_cx != state.base_width || normalized.base_cy != state.base_height))
		return false;

	if (normalized.use_output_res &&
	    (normalized.output_cx != state.output_width || normalized.output_cy != state.output_height))
		return false;

	if (normalized.use_fps) {
		if (state.fps_den == 0)
			return false;
		if (static_cast<uint64_t>(normalized.fps) * state.fps_den != state.fps_num)
			return false;
	}

	return true;
}

uint32_t first_selected_audio_track(uint32_t track_mask)
{
	track_mask &= kRecordingTrackMask;
	for (uint32_t index = 0; index < 6; ++index) {
		if ((track_mask & (1u << index)) != 0)
			return index + 1;
	}
	return 0;
}

uint32_t rounded_integer_fps(uint32_t numerator, uint32_t denominator)
{
	if (denominator == 0)
		return 0;

	return static_cast<uint32_t>((static_cast<uint64_t>(numerator) + denominator / 2u) / denominator);
}
