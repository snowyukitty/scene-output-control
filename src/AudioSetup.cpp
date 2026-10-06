// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioSetup.hpp"

#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

#include <cstdio>

namespace {

template<typename T> struct ComPtr {
	T *ptr = nullptr;
	~ComPtr()
	{
		if (ptr)
			ptr->Release();
	}
	ComPtr() = default;
	ComPtr(const ComPtr &) = delete;
	ComPtr &operator=(const ComPtr &) = delete;
	T *operator->() const { return ptr; }
};

struct ComScope {
	HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	~ComScope()
	{
		if (SUCCEEDED(result))
			CoUninitialize();
	}
	bool available() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};

struct PropertyValue {
	PROPVARIANT value{};
	~PropertyValue() { PropVariantClear(&value); }
};

// Windows Sound's Listen tab stores these properties in the endpoint's
// IPropertyStore. The enabled property is VT_BOOL; an empty destination means
// the default playback device. Do not write registry blobs directly.
constexpr PROPERTYKEY kListenEnabled = {{0x24dbb0fc, 0x9311, 0x4b3d, {0x9c, 0xf0, 0x18, 0xff, 0x15, 0x56, 0x39, 0xd4}},
					1};
constexpr PROPERTYKEY kListenDestination = {
	{0x24dbb0fc, 0x9311, 0x4b3d, {0x9c, 0xf0, 0x18, 0xff, 0x15, 0x56, 0x39, 0xd4}},
	0};

std::string utf8(const wchar_t *value)
{
	if (!value || !*value)
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 0)
		return {};
	std::string text(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), size, nullptr, nullptr);
	text.pop_back();
	return text;
}

std::wstring wide(const std::string &value)
{
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
	if (size <= 0)
		return {};
	std::wstring text(size, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, text.data(), size);
	text.pop_back();
	return text;
}

std::string failure(HRESULT result)
{
	if (result == E_ACCESSDENIED || result == STG_E_ACCESSDENIED)
		return "Windows denied access. Open Windows Sound, choose the recording device's "
		       "Properties, then turn off Listen to this device on the Listen tab.";
	char message[64];
	std::snprintf(message, sizeof(message), "Windows audio settings error: 0x%08lX",
		      static_cast<unsigned long>(result));
	return message;
}

std::string device_name(IMMDevice *device)
{
	ComPtr<IPropertyStore> properties;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &properties.ptr)))
		return {};
	PropertyValue name;
	if (FAILED(properties->GetValue(PKEY_Device_FriendlyName, &name.value)) || name.value.vt != VT_LPWSTR)
		return {};
	return utf8(name.value.pwszVal);
}

HRESULT read_enabled(IPropertyStore *properties, bool &enabled)
{
	PropertyValue value;
	const HRESULT result = properties->GetValue(kListenEnabled, &value.value);
	if (FAILED(result))
		return result;
	if (value.value.vt == VT_EMPTY) {
		enabled = false;
		return S_OK;
	}
	if (value.value.vt != VT_BOOL)
		return E_UNEXPECTED;
	// VARIANT_TRUE is -1, not 1.
	enabled = value.value.boolVal != VARIANT_FALSE;
	return S_OK;
}

} // namespace
#endif

AudioSetupCheck check_audio_setup()
{
	AudioSetupCheck check;
#ifdef _WIN32
	check.windows_listen_supported = true;
	ComScope com;
	if (!com.available()) {
		check.error = failure(com.result);
		return check;
	}
	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
					  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator.ptr));
	ComPtr<IMMDeviceCollection> devices;
	if (SUCCEEDED(result))
		result = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices.ptr);
	UINT count = 0;
	if (SUCCEEDED(result))
		result = devices->GetCount(&count);
	if (FAILED(result)) {
		check.error = failure(result);
		return check;
	}
	check.complete = true;
	for (UINT i = 0; i < count; ++i) {
		ComPtr<IMMDevice> device;
		ComPtr<IPropertyStore> properties;
		result = devices->Item(i, &device.ptr);
		if (SUCCEEDED(result))
			result = device->OpenPropertyStore(STGM_READ, &properties.ptr);
		bool enabled = false;
		if (SUCCEEDED(result))
			result = read_enabled(properties.ptr, enabled);
		if (FAILED(result)) {
			check.complete = false;
			check.error = failure(result);
			continue;
		}
		if (!enabled)
			continue;

		LPWSTR id = nullptr;
		result = device->GetId(&id);
		if (FAILED(result)) {
			check.complete = false;
			check.error = failure(result);
			continue;
		}
		WindowsListenDevice listening{utf8(id), device_name(device.ptr), {}};
		CoTaskMemFree(id);
		if (listening.name.empty())
			listening.name = "Unnamed recording device";
		PropertyValue destination;
		result = properties->GetValue(kListenDestination, &destination.value);
		if (SUCCEEDED(result)) {
			if (destination.value.vt == VT_EMPTY ||
			    (destination.value.vt == VT_LPWSTR &&
			     (!destination.value.pwszVal || !*destination.value.pwszVal))) {
				listening.playback_name = "Default playback device";
			} else if (destination.value.vt == VT_LPWSTR) {
				ComPtr<IMMDevice> playback;
				if (SUCCEEDED(enumerator->GetDevice(destination.value.pwszVal, &playback.ptr)))
					listening.playback_name = device_name(playback.ptr);
			}
		}
		if (listening.playback_name.empty())
			listening.playback_name = "Unknown playback device";
		check.listening_devices.push_back(std::move(listening));
	}
#endif
	return check;
}

bool disable_windows_listen(const std::string &device_id, std::string &error)
{
#ifdef _WIN32
	const std::wstring id = wide(device_id);
	if (id.empty()) {
		error = "No recording device selected.";
		return false;
	}
	ComScope com;
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDevice> device;
	ComPtr<IPropertyStore> properties;
	HRESULT result = com.available() ? CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
							    __uuidof(IMMDeviceEnumerator),
							    reinterpret_cast<void **>(&enumerator.ptr))
					 : com.result;
	if (SUCCEEDED(result))
		result = enumerator->GetDevice(id.c_str(), &device.ptr);
	if (SUCCEEDED(result))
		result = device->OpenPropertyStore(STGM_READWRITE, &properties.ptr);
	bool original = false;
	if (SUCCEEDED(result))
		result = read_enabled(properties.ptr, original);
	if (FAILED(result)) {
		error = failure(result);
		return false;
	}
	if (!original)
		return true;

	PropertyValue value;
	value.value.vt = VT_BOOL;
	value.value.boolVal = VARIANT_FALSE;
	result = properties->SetValue(kListenEnabled, value.value);
	if (SUCCEEDED(result))
		result = properties->Commit();
	// Open a fresh store to verify the persisted setting.
	ComPtr<IPropertyStore> verification;
	if (SUCCEEDED(result))
		result = device->OpenPropertyStore(STGM_READ, &verification.ptr);
	bool enabled = true;
	if (SUCCEEDED(result))
		result = read_enabled(verification.ptr, enabled);
	if (SUCCEEDED(result) && !enabled)
		return true;

	error = FAILED(result) ? failure(result) : "Windows Listen is still enabled.";
	value.value.boolVal = VARIANT_TRUE;
	ComPtr<IPropertyStore> recovery;
	HRESULT restore = device->OpenPropertyStore(STGM_READWRITE, &recovery.ptr);
	if (SUCCEEDED(restore))
		restore = recovery->SetValue(kListenEnabled, value.value);
	if (FAILED(restore) || FAILED(recovery->Commit()))
		error += " Could not restore the previous setting; check Windows Sound settings.";
	return false;
#else
	(void)device_id;
	error = "Windows Listen settings are available on Windows only.";
	return false;
#endif
}
