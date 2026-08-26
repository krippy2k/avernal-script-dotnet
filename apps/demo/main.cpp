#include <avernal/dotnet/dotnet.hpp>

#include <print>

int main() {
    avernal::DotnetScriptEngine engine;
    const auto scripts = engine.managed_dir().parent_path() / "scripts" / "Spin.cs";
    if (!engine.load(scripts)) {
        std::println("failed to load {}", scripts.string());
        return 1;
    }

    auto script = engine.instantiate("Spin");
    if (!script) {
        std::println("failed to instantiate Spin");
        return 1;
    }

    script->on_create();
    for (int tick = 0; tick < 3; ++tick) {
        script->on_update(0.016f);
    }
    script->on_destroy();

    std::println("type = {}", script->type_name());
    return 0;
}
