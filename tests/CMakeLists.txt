include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(avernal-script-dotnet-tests dotnet_test.cpp)
target_link_libraries(avernal-script-dotnet-tests PRIVATE avernal::dotnet GTest::gtest_main)
avernal_dotnet_prepare_target(
    avernal-script-dotnet-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(avernal-script-dotnet-tests PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(avernal-script-dotnet-tests PRIVATE /W4)
endif()

include(GoogleTest)
gtest_discover_tests(avernal-script-dotnet-tests DISCOVERY_MODE PRE_TEST)
