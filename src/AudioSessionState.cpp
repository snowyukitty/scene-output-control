// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioSessionState.hpp"

AudioSessionSnapshotMatch audio_session_snapshot_match(const AudioSessionMuteSnapshot &snapshot,
						       const std::string &endpoint_id, const std::string &session_id,
						       const std::string &instance_id)
{
	if (snapshot.endpoint_id.empty() || snapshot.endpoint_id != endpoint_id)
		return AudioSessionSnapshotMatch::None;

	if (!snapshot.instance_id.empty() && !instance_id.empty() && snapshot.instance_id == instance_id)
		return AudioSessionSnapshotMatch::ExactInstance;

	if (!snapshot.session_id.empty() && !session_id.empty() && snapshot.session_id == session_id)
		return AudioSessionSnapshotMatch::StableSession;

	return AudioSessionSnapshotMatch::None;
}

std::size_t find_audio_session_snapshot(const std::vector<AudioSessionMuteSnapshot> &snapshots,
					const std::string &endpoint_id, const std::string &session_id,
					const std::string &instance_id)
{
	std::size_t stable_match = kAudioSessionSnapshotNotFound;
	for (std::size_t index = 0; index < snapshots.size(); ++index) {
		switch (audio_session_snapshot_match(snapshots[index], endpoint_id, session_id, instance_id)) {
		case AudioSessionSnapshotMatch::ExactInstance:
			return index;
		case AudioSessionSnapshotMatch::StableSession:
			if (stable_match == kAudioSessionSnapshotNotFound)
				stable_match = index;
			break;
		case AudioSessionSnapshotMatch::None:
			break;
		}
	}
	return stable_match;
}
