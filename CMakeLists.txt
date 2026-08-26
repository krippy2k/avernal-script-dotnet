cmake_minimum_required(VERSION 3.28)

project(
    avernal-script-dotnet
    VERSION 0.1.0
    DESCRIPTION ".NET scripting backend for the Avernal game engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT WIN32)
    message(FATAL_ERROR "avernal-script-dotnet currently supports Windows.")
endif()

include(FetchContent)

set(AVERNAL_CORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-core")
set(AVERNAL_SCRIPT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-script")

if(NOT EXISTS "${AVERNAL_CORE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "avernal-core not found at:\n"
        "  ${AVERNAL_CORE_SOURCE_DIR}\n"
        "Place avernal-core as a sibling of this project, or set "
        "FETCHCONTENT_SOURCE_DIR_AVERNAL_CORE.")
endif()

if(NOT EXISTS "${AVERNAL_SCRIPT_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "avernal-script not found at:\n"
        "  ${AVERNAL_SCRIPT_SOURCE_DIR}\n"
        "Place avernal-script as a sibling of this project, or set "
        "FETCHCONTENT_SOURCE_DIR_AVERNAL_SCRIPT.")
endif()

FetchContent_Declare(
    avernal_core
    SOURCE_DIR "${AVERNAL_CORE_SOURCE_DIR}"
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(
    avernal_script
    SOURCE_DIR "${AVERNAL_SCRIPT_SOURCE_DIR}"
    EXCLUDE_FROM_ALL
)

set(AVERNAL_CORE_BUILD_DEMO OFF)
set(AVERNAL_SCRIPT_BUILD_DEMO OFF)

FetchContent_MakeAvailable(avernal_core avernal_script)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    set(AVERNAL_DOTNET_RID "win-arm64")
else()
    set(AVERNAL_DOTNET_RID "win-x64")
endif()

if(NOT AVERNAL_NETHOST_DIR)
    set(_avernal_dotnet_root "$ENV{ProgramFiles}/dotnet")
    file(GLOB _avernal_nethost_versions
        LIST_DIRECTORIES true
        "${_avernal_dotnet_root}/packs/Microsoft.NETCore.App.Host.${AVERNAL_DOTNET_RID}/*")
    if(NOT _avernal_nethost_versions)
        message(FATAL_ERROR
            "nethost not found under:\n"
            "  ${_avernal_dotnet_root}/packs/Microsoft.NETCore.App.Host.${AVERNAL_DOTNET_RID}\n"
            "Install the .NET SDK/runtime, or set AVERNAL_NETHOST_DIR.")
    endif()
    list(SORT _avernal_nethost_versions COMPARE NATURAL ORDER DESCENDING)
    list(GET _avernal_nethost_versions 0 _avernal_nethost_version)
    set(AVERNAL_NETHOST_DIR
        "${_avernal_nethost_version}/runtimes/${AVERNAL_DOTNET_RID}/native")
endif()

if(NOT EXISTS "${AVERNAL_NETHOST_DIR}/nethost.h")
    message(FATAL_ERROR "nethost.h not found in AVERNAL_NETHOST_DIR:\n  ${AVERNAL_NETHOST_DIR}")
endif()

set(AVERNAL_NETHOST_DLL "${AVERNAL_NETHOST_DIR}/nethost.dll" CACHE INTERNAL "nethost.dll for Avernal .NET hosting")
set(AVERNAL_NETHOST_LIB "${AVERNAL_NETHOST_DIR}/nethost.lib")
set(AVERNAL_MANAGED_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/managed" CACHE INTERNAL "Avernal.Scripting publish output")

find_program(DOTNET_EXECUTABLE
    NAMES dotnet
    HINTS
        "$ENV{DOTNET_ROOT}"
        "$ENV{ProgramFiles}/dotnet"
    REQUIRED
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(AVERNAL_DOTNET_CONFIG Release)
else()
    set(AVERNAL_DOTNET_CONFIG Debug)
endif()

set(AVERNAL_MANAGED_PROJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/managed/Avernal.Scripting/Avernal.Scripting.csproj")

add_custom_command(
    OUTPUT
        "${AVERNAL_MANAGED_OUTPUT_DIR}/Avernal.Scripting.dll"
        "${AVERNAL_MANAGED_OUTPUT_DIR}/Avernal.Scripting.runtimeconfig.json"
    COMMAND
        "${DOTNET_EXECUTABLE}" publish
        "${AVERNAL_MANAGED_PROJECT}"
        -c "${AVERNAL_DOTNET_CONFIG}"
        -o "${AVERNAL_MANAGED_OUTPUT_DIR}"
        --nologo
    DEPENDS
        "${AVERNAL_MANAGED_PROJECT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/managed/Avernal.Scripting/Script.cs"
        "${CMAKE_CURRENT_SOURCE_DIR}/managed/Avernal.Scripting/NativeHost.cs"
    COMMENT "Publishing Avernal.Scripting"
    VERBATIM
)

add_custom_target(
    avernal-scripting-managed ALL
    DEPENDS "${AVERNAL_MANAGED_OUTPUT_DIR}/Avernal.Scripting.dll"
)

function(avernal_dotnet_prepare_target target)
    add_dependencies(${target} avernal-scripting-managed)
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}.managed.stamp")
    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/managed"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${AVERNAL_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${target}>/managed"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${AVERNAL_NETHOST_DLL}"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS
            "${AVERNAL_MANAGED_OUTPUT_DIR}/Avernal.Scripting.dll"
            ${target}
        VERBATIM
    )
    add_custom_target(${target}-managed ALL DEPENDS "${_stamp}")
    if(ARGC GREATER 1)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ARGV1}"
                "$<TARGET_FILE_DIR:${target}>/scripts"
            VERBATIM
        )
    endif()
endfunction()

add_library(avernal-script-dotnet)
add_library(avernal::dotnet ALIAS avernal-script-dotnet)

target_sources(
    avernal-script-dotnet
    PRIVATE src/dotnet.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/avernal/dotnet/dotnet.hpp
)

target_include_directories(avernal-script-dotnet PRIVATE "${AVERNAL_NETHOST_DIR}")
target_link_libraries(
    avernal-script-dotnet
    PUBLIC
        avernal::script
        "${AVERNAL_NETHOST_LIB}"
)
target_compile_features(avernal-script-dotnet PUBLIC cxx_std_23)
add_dependencies(avernal-script-dotnet avernal-scripting-managed)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(avernal-script-dotnet PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(avernal-script-dotnet PRIVATE /W4)
endif()

option(AVERNAL_DOTNET_BUILD_DEMO "Build the Avernal .NET scripting demo executable" ON)

if(AVERNAL_DOTNET_BUILD_DEMO)
    add_executable(avernal-script-dotnet-demo apps/demo/main.cpp)
    target_link_libraries(avernal-script-dotnet-demo PRIVATE avernal::dotnet)
    avernal_dotnet_prepare_target(
        avernal-script-dotnet-demo
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/demo/scripts"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(avernal-script-dotnet-demo PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(avernal-script-dotnet-demo PRIVATE /W4)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    option(AVERNAL_DOTNET_BUILD_TESTS "Build Avernal .NET scripting unit tests" ON)
else()
    option(AVERNAL_DOTNET_BUILD_TESTS "Build Avernal .NET scripting unit tests" OFF)
endif()

if(AVERNAL_DOTNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
