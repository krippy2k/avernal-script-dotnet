#pragma once

#include <avernal/script/engine.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace avernal {

class DotnetScriptEngine final : public ScriptEngine {
public:
    explicit DotnetScriptEngine(std::filesystem::path managed_dir = {});
    ~DotnetScriptEngine() override;

    bool load(const std::filesystem::path& path) override;
    [[nodiscard]] bool has_type(std::string_view type_name) const override;
    [[nodiscard]] std::unique_ptr<Script> instantiate(std::string_view type_name) override;

    [[nodiscard]] const std::filesystem::path& managed_dir() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace avernal
