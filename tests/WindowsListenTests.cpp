// SPDX-License-Identifier: GPL-2.0-or-later

// Exercise the private Windows transaction without exposing test-only plugin
// APIs. This translation unit compiles the same implementation as the plugin;
// every endpoint/property store below is in memory, never a real audio device.
#include "../src/AudioSetup.cpp"

#include <iostream>
#include <memory>

namespace {

int failures = 0;
void check(bool condition, const char *expression, int line)
{
	if (!condition) {
		std::cerr << "line " << line << ": check failed: " << expression << '\n';
		++failures;
	}
}
#define CHECK(expression) check((expression), #expression, __LINE__)

struct FakeDevice;

struct FakeStore : IPropertyStore {
	FakeDevice &device;
	bool cached;
	bool verification;
	bool committed = false;
	ULONG references = 0;

	FakeStore(FakeDevice &owner, bool enabled, bool verify) : device(owner), cached(enabled), verification(verify)
	{
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override
	{
		*object = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override { return --references; }
	HRESULT STDMETHODCALLTYPE GetCount(DWORD *) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetAt(DWORD, PROPERTYKEY *) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY key, PROPVARIANT *value) override;
	HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY key, REFPROPVARIANT value) override;
	HRESULT STDMETHODCALLTYPE Commit() override;
};

struct FakeDevice : IMMDevice {
	bool persisted = true;
	VARTYPE type = VT_BOOL;
	HRESULT read_result = S_OK;
	bool verification_still_enabled = false;
	int fail_open = 0;
	int fail_write = 0;
	int fail_commit = 0;
	int writes = 0;
	int commits = 0;
	std::vector<DWORD> modes;
	std::vector<std::unique_ptr<FakeStore>> stores;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override
	{
		*object = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
	HRESULT STDMETHODCALLTYPE Activate(REFIID, DWORD, PROPVARIANT *, void **) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetId(LPWSTR *) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetState(DWORD *) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD mode, IPropertyStore **store) override
	{
		*store = nullptr;
		modes.push_back(mode);
		if (static_cast<int>(modes.size()) == fail_open)
			return E_ACCESSDENIED;
		auto created = std::make_unique<FakeStore>(*this, persisted, mode == STGM_READ);
		created->AddRef();
		*store = created.get();
		stores.push_back(std::move(created));
		return S_OK;
	}
	bool stores_released() const
	{
		for (const auto &store : stores) {
			if (store->references != 0)
				return false;
		}
		return true;
	}
};

HRESULT FakeStore::GetValue(REFPROPERTYKEY key, PROPVARIANT *value)
{
	CHECK(IsEqualPropertyKey(key, kListenEnabled));
	if (committed)
		return E_FAIL;
	if (FAILED(device.read_result))
		return device.read_result;
	value->vt = device.type;
	value->boolVal = (verification && device.verification_still_enabled) || cached ? VARIANT_TRUE : VARIANT_FALSE;
	return S_OK;
}

HRESULT FakeStore::SetValue(REFPROPERTYKEY key, REFPROPVARIANT value)
{
	CHECK(IsEqualPropertyKey(key, kListenEnabled));
	CHECK(value.vt == VT_BOOL);
	if (committed)
		return E_FAIL;
	if (++device.writes == device.fail_write)
		return E_ACCESSDENIED;
	cached = value.boolVal != VARIANT_FALSE;
	return S_OK;
}

HRESULT FakeStore::Commit()
{
	// A committed property store is no longer reusable. Verification and
	// rollback must open a new store, as required by IPropertyStore::Commit.
	if (committed)
		return E_FAIL;
	committed = true;
	if (++device.commits == device.fail_commit)
		return E_FAIL;
	device.persisted = cached;
	return S_OK;
}

void test_read_types()
{
	FakeDevice device;
	FakeStore store(device, true, false);
	bool enabled = false;
	CHECK(SUCCEEDED(read_enabled(&store, enabled)) && enabled);
	store.cached = false;
	CHECK(SUCCEEDED(read_enabled(&store, enabled)) && !enabled);
	device.type = VT_EMPTY;
	store.cached = true;
	CHECK(SUCCEEDED(read_enabled(&store, enabled)) && !enabled);
	device.type = VT_UI4;
	CHECK(read_enabled(&store, enabled) == E_UNEXPECTED);
	device.read_result = E_ACCESSDENIED;
	CHECK(read_enabled(&store, enabled) == E_ACCESSDENIED);
}

void test_verified_write_and_noop()
{
	FakeDevice device;
	std::string error = "previous error";
	CHECK(disable_listen_on_device(&device, error));
	CHECK(!device.persisted && error.empty());
	CHECK(device.writes == 1 && device.commits == 1);
	CHECK(device.modes.size() == 2 && device.modes[1] == STGM_READ);
	CHECK(device.stores_released());
	CHECK(disable_listen_on_device(&device, error));
	CHECK(device.writes == 1 && device.commits == 1);
	CHECK(device.stores_released());
}

void test_refused_reads_never_write()
{
	for (bool unexpected_type : {false, true}) {
		FakeDevice device;
		if (unexpected_type)
			device.type = VT_UI4;
		else
			device.read_result = E_ACCESSDENIED;
		std::string error;
		CHECK(!disable_listen_on_device(&device, error));
		CHECK(device.persisted && device.writes == 0 && !error.empty());
		CHECK(device.stores_released());
	}
}

void test_rollback()
{
	for (int scenario = 0; scenario < 4; ++scenario) {
		FakeDevice device;
		if (scenario == 0)
			device.fail_write = 1;
		else if (scenario == 1)
			device.fail_commit = 1;
		else if (scenario == 2)
			device.fail_open = 2;
		else
			device.verification_still_enabled = true;
		std::string error;
		CHECK(!disable_listen_on_device(&device, error));
		CHECK(device.persisted && !error.empty());
		CHECK(device.stores_released());
	}
}

void test_failed_recovery_is_reported()
{
	FakeDevice device;
	device.fail_open = 2;
	device.fail_commit = 2;
	std::string error;
	CHECK(!disable_listen_on_device(&device, error));
	CHECK(!device.persisted);
	CHECK(error.find("Could not restore the previous setting") != std::string::npos);
	CHECK(device.stores_released());
}

} // namespace

int main()
{
	test_read_types();
	test_verified_write_and_noop();
	test_refused_reads_never_write();
	test_rollback();
	test_failed_recovery_is_reported();
	return failures ? 1 : 0;
}
