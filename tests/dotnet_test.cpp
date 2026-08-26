#include <avernal/dotnet/dotnet.hpp>

#include <gtest/gtest.h>

#include <filesystem>

namespace {

[[nodiscard]] std::filesystem::path scripts_dir(const avernal::DotnetScriptEngine& engine) {
    return engine.managed_dir().parent_path() / "scripts";
}

}  // namespace

TEST(DotnetScriptEngine, LoadsCSharpFile) {
    avernal::DotnetScriptEngine engine;
    ASSERT_TRUE(std::filesystem::is_regular_file(engine.managed_dir() / "Avernal.Scripting.dll"));
    ASSERT_TRUE(engine.load(scripts_dir(engine) / "Counter.cs"));
    EXPECT_TRUE(engine.has_type("Counter"));
    EXPECT_FALSE(engine.has_type("Missing"));
}

TEST(DotnetScriptEngine, InstantiatesAndUpdates) {
    avernal::DotnetScriptEngine engine;
    ASSERT_TRUE(engine.load(scripts_dir(engine) / "Counter.cs"));

    auto script = engine.instantiate("Counter");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->type_name(), "Counter");

    script->on_create();
    script->on_update(0.016f);
    script->on_update(0.016f);
    script->on_destroy();
}

TEST(DotnetScriptEngine, UnknownTypeReturnsNull) {
    avernal::DotnetScriptEngine engine;
    EXPECT_EQ(engine.instantiate("Missing"), nullptr);
}

TEST(DotnetScriptEngine, MissingFileFails) {
    avernal::DotnetScriptEngine engine;
    EXPECT_FALSE(engine.load(scripts_dir(engine) / "nope.cs"));
}

TEST(DotnetScriptEngine, LoadsScriptDirectory) {
    avernal::DotnetScriptEngine engine;
    ASSERT_TRUE(engine.load(scripts_dir(engine)));
    EXPECT_TRUE(engine.has_type("Counter"));
    EXPECT_TRUE(engine.has_type("BindSpin"));
    EXPECT_NE(engine.instantiate("Counter"), nullptr);
}

TEST(DotnetScriptEngine, BindsNativeTransform) {
    struct NativeTransform {
        float px{}, py{}, pz{};
        float rx{}, ry{}, rz{};
        float sx{1.0f}, sy{1.0f}, sz{1.0f};
    };

    NativeTransform transform{};
    avernal::DotnetScriptEngine engine;
    ASSERT_TRUE(engine.load(scripts_dir(engine) / "BindSpin.cs"));

    auto script = engine.instantiate("BindSpin");
    ASSERT_NE(script, nullptr);
    script->set_user_data(&transform);
    script->on_update(2.0f);

    EXPECT_FLOAT_EQ(transform.ry, 2.0f);
    EXPECT_FLOAT_EQ(transform.rx, 0.0f);
    EXPECT_FLOAT_EQ(transform.sx, 1.0f);
}
