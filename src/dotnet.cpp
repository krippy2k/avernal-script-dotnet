#include <avernal/core/assert.hpp>
#include <avernal/dotnet/dotnet.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace avernal {
namespace {

using load_fn = int(CORECLR_DELEGATE_CALLTYPE*)(const char* path);
using has_type_fn = int(CORECLR_DELEGATE_CALLTYPE*)(const char* name);
using create_fn = std::intptr_t(CORECLR_DELEGATE_CALLTYPE*)(const char* name);
using on_create_fn = void(CORECLR_DELEGATE_CALLTYPE*)(std::intptr_t handle);
using on_update_fn = void(CORECLR_DELEGATE_CALLTYPE*)(std::intptr_t handle, float delta_time);
using on_destroy_fn = void(CORECLR_DELEGATE_CALLTYPE*)(std::intptr_t handle);
using release_fn = void(CORECLR_DELEGATE_CALLTYPE*)(std::intptr_t handle);
using bind_user_data_fn = void(CORECLR_DELEGATE_CALLTYPE*)(std::intptr_t handle, std::intptr_t user_data);

struct ManagedApi {
    on_create_fn on_create{};
    on_update_fn on_update{};
    on_destroy_fn on_destroy{};
    release_fn release{};
    bind_user_data_fn bind_user_data{};
};

[[nodiscard]] constexpr bool hostfxr_ok(std::int32_t rc) noexcept {
    return rc == 0 || rc == 1 || rc == 2;
}

[[nodiscard]] std::filesystem::path executable_path() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    AV_ENSURE(length > 0 && length < MAX_PATH);
    return std::filesystem::path(buffer);
}

[[nodiscard]] std::filesystem::path executable_directory() {
    return executable_path().parent_path();
}

template<typename T>
[[nodiscard]] T get_export(HMODULE module, const char* name) {
    const FARPROC symbol = GetProcAddress(module, name);
    AV_ENSURE(symbol != nullptr);
    return reinterpret_cast<T>(symbol);
}

class DotnetScript final : public Script {
public:
    DotnetScript(ManagedApi api, std::intptr_t handle, std::string type_name)
        : api_(api), handle_(handle), type_name_(std::move(type_name)) {}

    DotnetScript(const DotnetScript&) = delete;
    DotnetScript& operator=(const DotnetScript&) = delete;

    ~DotnetScript() override {
        if (handle_ == 0) {
            return;
        }
        if (!destroyed_) {
            api_.on_destroy(handle_);
        }
        api_.release(handle_);
        handle_ = 0;
    }

    void on_create() override {
        if (handle_ != 0) {
            api_.on_create(handle_);
        }
    }

    void on_update(float delta_time) override {
        if (handle_ != 0) {
            api_.on_update(handle_, delta_time);
        }
    }

    void on_destroy() override {
        if (handle_ == 0 || destroyed_) {
            return;
        }
        api_.on_destroy(handle_);
        destroyed_ = true;
    }

    void set_user_data(void* data) noexcept override {
        Script::set_user_data(data);
        if (handle_ != 0) {
            api_.bind_user_data(handle_, reinterpret_cast<std::intptr_t>(data));
        }
    }

    [[nodiscard]] std::string_view type_name() const noexcept override { return type_name_; }

private:
    ManagedApi api_{};
    std::intptr_t handle_{};
    std::string type_name_{};
    bool destroyed_{};
};

}  // namespace

class DotnetScriptEngine::Impl {
public:
    std::filesystem::path managed_dir{};
    load_fn load{};
    has_type_fn has_type{};
    create_fn create{};
    ManagedApi api{};
};

DotnetScriptEngine::DotnetScriptEngine(std::filesystem::path managed_dir)
    : impl_(std::make_unique<Impl>()) {
    if (managed_dir.empty()) {
        managed_dir = executable_directory() / "managed";
    }
    impl_->managed_dir = std::filesystem::absolute(managed_dir);

    const auto assembly = impl_->managed_dir / "Avernal.Scripting.dll";
    const auto runtimeconfig = impl_->managed_dir / "Avernal.Scripting.runtimeconfig.json";
    AV_ENSURE(std::filesystem::is_regular_file(assembly));
    AV_ENSURE(std::filesystem::is_regular_file(runtimeconfig));

    std::wstring hostfxr_path(1024, L'\0');
    size_t hostfxr_size = hostfxr_path.size();
    int rc = get_hostfxr_path(hostfxr_path.data(), &hostfxr_size, nullptr);
    if (rc == static_cast<int>(0x80008098)) {
        hostfxr_path.resize(hostfxr_size);
        rc = get_hostfxr_path(hostfxr_path.data(), &hostfxr_size, nullptr);
    }
    AV_ENSURE(rc == 0);
    hostfxr_path.resize(std::wcslen(hostfxr_path.c_str()));

    const HMODULE hostfxr = LoadLibraryW(hostfxr_path.c_str());
    AV_ENSURE(hostfxr != nullptr);

    const auto initialize = get_export<hostfxr_initialize_for_runtime_config_fn>(
        hostfxr, "hostfxr_initialize_for_runtime_config");
    const auto get_delegate =
        get_export<hostfxr_get_runtime_delegate_fn>(hostfxr, "hostfxr_get_runtime_delegate");
    const auto close = get_export<hostfxr_close_fn>(hostfxr, "hostfxr_close");

    const std::wstring exe = executable_path().wstring();
    hostfxr_initialize_parameters params{};
    params.size = sizeof(params);
    params.host_path = exe.c_str();

    const std::wstring config_path = runtimeconfig.wstring();
    hostfxr_handle context = nullptr;
    rc = initialize(config_path.c_str(), &params, &context);
    AV_ENSURE(hostfxr_ok(rc) && context != nullptr);

    load_assembly_and_get_function_pointer_fn load_assembly{};
    rc = get_delegate(
        context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&load_assembly));
    close(context);
    AV_ENSURE(rc == 0 && load_assembly != nullptr);

    const std::wstring assembly_path = assembly.wstring();
    const char_t* type_name = L"Avernal.Scripting.NativeHost, Avernal.Scripting";

    auto bind = [&](const char_t* method, void** out) {
        const int bind_rc = load_assembly(
            assembly_path.c_str(), type_name, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, out);
        AV_ENSURE(bind_rc == 0 && *out != nullptr);
    };

    bind(L"Load", reinterpret_cast<void**>(&impl_->load));
    bind(L"HasType", reinterpret_cast<void**>(&impl_->has_type));
    bind(L"Create", reinterpret_cast<void**>(&impl_->create));
    bind(L"OnCreate", reinterpret_cast<void**>(&impl_->api.on_create));
    bind(L"OnUpdate", reinterpret_cast<void**>(&impl_->api.on_update));
    bind(L"OnDestroy", reinterpret_cast<void**>(&impl_->api.on_destroy));
    bind(L"Release", reinterpret_cast<void**>(&impl_->api.release));
    bind(L"BindUserData", reinterpret_cast<void**>(&impl_->api.bind_user_data));
}

DotnetScriptEngine::~DotnetScriptEngine() = default;

bool DotnetScriptEngine::load(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    const std::string utf8(reinterpret_cast<const char*>(u8.data()), u8.size());
    return impl_->load(utf8.c_str()) != 0;
}

bool DotnetScriptEngine::has_type(std::string_view type_name) const {
    const std::string utf8(type_name);
    return impl_->has_type(utf8.c_str()) != 0;
}

std::unique_ptr<Script> DotnetScriptEngine::instantiate(std::string_view type_name) {
    const std::string utf8(type_name);
    const std::intptr_t handle = impl_->create(utf8.c_str());
    if (handle == 0) {
        return nullptr;
    }
    return std::make_unique<DotnetScript>(impl_->api, handle, utf8);
}

const std::filesystem::path& DotnetScriptEngine::managed_dir() const noexcept {
    return impl_->managed_dir;
}

}  // namespace avernal
