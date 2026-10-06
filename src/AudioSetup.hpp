// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

struct WindowsListenDevice {
	std::string id;
	std::string name;
	std::string playback_name;
};

struct AudioSetupCheck {
	bool windows_listen_supported = false;
	bool complete = false;
	std::vector<WindowsListenDevice> listening_devices;
	std::string error;
};

// Inspect Windows playback routes without changing source or device settings.
AudioSetupCheck check_audio_setup();

// Disable only the selected device's Windows Listen setting. Read back the
// committed value and retain the original value if verification fails.
bool disable_windows_listen(const std::string &device_id, std::string &error);
