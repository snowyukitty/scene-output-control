// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioSessionState.hpp"
#include "PresetValidation.hpp"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *expression, int line)
{
	if (condition)
		return;

	std::cerr << "line " << line << ": check failed: " << expression << '\n';
	++failures;
}

#define CHECK(expression) check((expression), #expression, __LINE__)

void test_numeric_normalization()
{
	ScenePreset preset;
	preset.enabled = true;
	preset.use_output_res = true;
	preset.output_cx = 1921;
	preset.output_cy = 1081;
	preset.audio_tracks = 0xc5;

	CHECK(normalize_scene_preset(preset));
	CHECK(preset.use_output_res);
	CHECK(preset.output_cx == 1920);
	CHECK(preset.output_cy == 1080);
	CHECK(preset.audio_tracks == 0x05);
	CHECK(!normalize_scene_preset(preset));

	preset.use_base_res = true;
	preset.base_cx = 1;
	preset.base_cy = 1080;
	CHECK(normalize_scene_preset(preset));
	CHECK(!preset.use_base_res);
	CHECK(preset.base_cx == 1920);
	CHECK(preset.base_cy == 1080);

	preset.use_output_res = true;
	preset.output_cx = 2;
	preset.output_cy = 2;
	CHECK(normalize_scene_preset(preset));
	CHECK(!preset.use_output_res);
	CHECK(preset.output_cx == 1920);
	CHECK(preset.output_cy == 1080);

	preset.use_fps = true;
	preset.fps = 0;
	CHECK(normalize_scene_preset(preset));
	CHECK(!preset.use_fps);
	CHECK(preset.fps == 60);

	preset.use_rec_bitrate = true;
	preset.rec_bitrate = 99;
	CHECK(normalize_scene_preset(preset));
	CHECK(!preset.use_rec_bitrate);
	CHECK(preset.rec_bitrate == 6000);
}

void test_video_match_is_selective_and_rational()
{
	VideoState state = {1920, 1080, 1920, 1080, 60000, 1000};
	ScenePreset preset;
	preset.enabled = true;

	CHECK(video_override_matches(preset, state));

	preset.use_output_res = true;
	preset.output_cx = 1921;
	preset.output_cy = 1081;
	CHECK(video_override_matches(preset, state));

	preset.use_base_res = true;
	preset.base_cx = 2560;
	preset.base_cy = 1440;
	CHECK(!video_override_matches(preset, state));

	preset.use_base_res = false;
	preset.use_output_res = false;
	preset.use_fps = true;
	preset.fps = 60;
	CHECK(video_override_matches(preset, state));

	state.fps_num = 60000;
	state.fps_den = 1001;
	CHECK(!video_override_matches(preset, state));
}

void test_flv_track_selection()
{
	CHECK(first_selected_audio_track(0) == 0);
	CHECK(first_selected_audio_track(1u << 0) == 1);
	CHECK(first_selected_audio_track((1u << 4) | (1u << 1)) == 2);
	CHECK(first_selected_audio_track(1u << 8) == 0);
}

void test_integer_fps_rounding()
{
	CHECK(rounded_integer_fps(60000, 1001) == 60);
	CHECK(rounded_integer_fps(30000, 1001) == 30);
	CHECK(rounded_integer_fps(25, 1) == 25);
	CHECK(rounded_integer_fps(1, 2) == 1);
	CHECK(rounded_integer_fps(60, 0) == 0);
}

void test_audio_session_snapshot_matching()
{
	const AudioSessionMuteSnapshot snapshot = {"endpoint-a", "stable-session", "instance-1", false};

	CHECK(audio_session_snapshot_match(snapshot, "endpoint-a", "stable-session", "instance-1") ==
	      AudioSessionSnapshotMatch::ExactInstance);
	CHECK(audio_session_snapshot_match(snapshot, "endpoint-a", "stable-session", "instance-2") ==
	      AudioSessionSnapshotMatch::StableSession);
	CHECK(audio_session_snapshot_match(snapshot, "endpoint-b", "stable-session", "instance-1") ==
	      AudioSessionSnapshotMatch::None);
	CHECK(audio_session_snapshot_match(snapshot, "endpoint-a", "other-session", "instance-2") ==
	      AudioSessionSnapshotMatch::None);

	const AudioSessionMuteSnapshot instanceOnly = {"endpoint-a", "", "instance-1", true};
	CHECK(audio_session_snapshot_match(instanceOnly, "endpoint-a", "", "instance-1") ==
	      AudioSessionSnapshotMatch::ExactInstance);
	CHECK(audio_session_snapshot_match(instanceOnly, "endpoint-a", "", "instance-2") ==
	      AudioSessionSnapshotMatch::None);

	const AudioSessionMuteSnapshot emptyEndpoint = {"", "stable-session", "instance-1", false};
	CHECK(audio_session_snapshot_match(emptyEndpoint, "", "stable-session", "instance-1") ==
	      AudioSessionSnapshotMatch::None);

	const std::vector<AudioSessionMuteSnapshot> snapshots = {
		{"endpoint-a", "stable-session", "old-instance", true},
		{"endpoint-a", "stable-session", "current-instance", false},
	};
	CHECK(find_audio_session_snapshot(snapshots, "endpoint-a", "stable-session", "current-instance") == 1);
	CHECK(find_audio_session_snapshot(snapshots, "endpoint-a", "stable-session", "future-instance") == 0);
	CHECK(find_audio_session_snapshot(snapshots, "endpoint-b", "stable-session", "current-instance") ==
	      kAudioSessionSnapshotNotFound);
}

} // namespace

int main()
{
	test_numeric_normalization();
	test_video_match_is_selective_and_rational();
	test_flv_track_selection();
	test_integer_fps_rounding();
	test_audio_session_snapshot_matching();

	if (failures != 0)
		std::cerr << failures << " test(s) failed\n";
	return failures == 0 ? 0 : 1;
}
