// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct AudioSessionMuteSnapshot {
	std::string endpoint_id;
	std::string session_id;
	std::string instance_id;
	bool muted = false;
	bool restored = false;
};

enum class AudioSessionSnapshotMatch {
	None = 0,
	StableSession = 1,
	ExactInstance = 2,
};

// Session instance IDs are exact for one Windows audio-session lifetime.
// Session IDs remain stable across session instances and allow recovery after
// an OBS crash/restart, but are shared by concurrent instances of the same
// application. Callers must filter by process before matching. Endpoint
// identity is always part of the match, and ExactInstance takes priority.
AudioSessionSnapshotMatch audio_session_snapshot_match(const AudioSessionMuteSnapshot &snapshot,
						       const std::string &endpoint_id, const std::string &session_id,
						       const std::string &instance_id);

constexpr std::size_t kAudioSessionSnapshotNotFound = static_cast<std::size_t>(-1);

// Returns the best matching snapshot, preferring an exact current instance
// even if an earlier snapshot has the same stable session identifier.
std::size_t find_audio_session_snapshot(const std::vector<AudioSessionMuteSnapshot> &snapshots,
					const std::string &endpoint_id, const std::string &session_id,
					const std::string &instance_id);
