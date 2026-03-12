cmake_minimum_required(VERSION 3.28)
project(genshin-fov-unlock
    VERSION 6.4.0.0
)

set(CMAKE_CXX_STANDARD 23)

if (NOT MSVC)
    message(FATAL_ERROR "This project requires MSVC to build.")
endif()

add_compile_options(
    /permissive-
    /utf-8
    /guard:cf
    /Qspectre
    "$<$<CONFIG:Release>:/GL>"
    "$<$<CONFIG:Release>:/Gw>"
)
add_link_options(
    /guard:cf
    /DYNAMICBASE
    /NXCOMPAT
    /HIGHENTROPYVA
    /CETCOMPAT
    "$<$<CONFIG:Release>:/LTCG>"
    "$<$<CONFIG:Release>:/DEBUG>"
    "$<$<CONFIG:Release>:/OPT:REF>"
    "$<$<CONFIG:Release>:/OPT:ICF>"
)

include(FetchContent)

FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v5.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

FetchContent_Declare(
    mminhook
    GIT_REPOSITORY https://github.com/z3lx/mminhook.git
    GIT_TAG 1.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mminhook)

add_compile_definitions(WIL_EXCEPTION_MODE=1)
set(WIL_BUILD_PACKAGING OFF)
set(WIL_BUILD_TESTS OFF)
FetchContent_Declare(
    wil
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG v1.0.250325.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(wil)

set(RESOURCES_DIR ${CMAKE_CURRENT_BINARY_DIR}/res)
file(COPY res/ DESTINATION ${RESOURCES_DIR})

set(VER_FILETYPE "VFT_APP")
set(VER_INTERNALNAME "loader")
set(VER_ORIGINALFILENAME "loader.exe")
set(VER_FILEDESCRIPTION "Genshin Impact Game Launcher")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/loader_version.rc
    @ONLY
)

set(VER_FILETYPE "VFT_DLL")
set(VER_INTERNALNAME "plugin")
set(VER_ORIGINALFILENAME "plugin.dll")
set(VER_FILEDESCRIPTION "Genshin Impact Configuration Plugin")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/plugin_version.rc
    @ONLY
)

# ── New-architecture resource configurations ──────────────────────────────────

set(VER_FILETYPE "VFT_APP")
set(VER_INTERNALNAME "launcher")
set(VER_ORIGINALFILENAME "launcher.exe")
set(VER_FILEDESCRIPTION "Genshin Impact Game Launcher")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/launcher_version.rc
    @ONLY
)

set(VER_FILETYPE "VFT_DLL")
set(VER_INTERNALNAME "bootstrap")
set(VER_ORIGINALFILENAME "bootstrap.dll")
set(VER_FILEDESCRIPTION "Genshin Impact Bootstrap DLL")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/bootstrap_version.rc
    @ONLY
)

set(VER_FILETYPE "VFT_DLL")
set(VER_INTERNALNAME "runtime")
set(VER_ORIGINALFILENAME "runtime.dll")
set(VER_FILEDESCRIPTION "Genshin Impact Runtime DLL")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/runtime_version.rc
    @ONLY
)

add_compile_definitions(WIN32_LEAN_AND_MEAN)

# ── util (shared utility library) ────────────────────────────────────────────

file(GLOB_RECURSE UTIL_SOURCES src/util/*)
add_library(util STATIC ${UTIL_SOURCES})
target_include_directories(util PRIVATE include)
target_link_libraries(util
    PRIVATE version.lib
    PUBLIC WIL::WIL
)

# ── shared (cross-component types and version whitelist) ─────────────────────

add_library(shared STATIC
    src/shared/Shared.cpp
)
target_include_directories(shared PUBLIC include)
target_link_libraries(shared PUBLIC WIL::WIL)

# ── Legacy targets (kept for reference; superseded by launcher/bootstrap/runtime) ──

file(GLOB_RECURSE LOADER_SOURCES src/loader/*)
set(LOADER_RESOURCE ${RESOURCES_DIR}/loader.rc)
add_executable(loader ${LOADER_SOURCES} ${LOADER_RESOURCE})
target_include_directories(loader PRIVATE include)
target_link_libraries(loader
    PRIVATE glaze::glaze
    PRIVATE WIL::WIL
    PRIVATE util
)
target_link_options(loader PRIVATE
    /MANIFEST:NO
)

file(GLOB_RECURSE PLUGIN_SOURCES src/plugin/*)
set(PLUGIN_RESOURCE ${RESOURCES_DIR}/plugin.rc)
add_library(plugin SHARED ${PLUGIN_SOURCES} ${PLUGIN_RESOURCE})
target_include_directories(plugin PRIVATE include)
target_link_libraries(plugin
    PRIVATE glaze::glaze
    PRIVATE mmh::mmh
    PRIVATE WIL::WIL
    PRIVATE util
)

# ── launcher (new-architecture executable) ────────────────────────────────────

set(LAUNCHER_SOURCES
    src/launcher/AppConfig.cpp
    src/launcher/GameLocator.cpp
    src/launcher/HandshakeClient.cpp
    src/launcher/Injector.cpp
    src/launcher/Main.cpp
    src/launcher/ProcessStarter.cpp
    src/launcher/VersionPolicy.cpp
)
set(LAUNCHER_RESOURCE ${RESOURCES_DIR}/launcher.rc)
add_executable(launcher ${LAUNCHER_SOURCES} ${LAUNCHER_RESOURCE})
target_include_directories(launcher PRIVATE include)
target_link_libraries(launcher
    PRIVATE glaze::glaze
    PRIVATE WIL::WIL
    PRIVATE shared
    PRIVATE util
)
target_link_options(launcher PRIVATE
    /MANIFEST:NO
)

# ── bootstrap (injected DLL that loads runtime.dll) ──────────────────────────

set(BOOTSTRAP_SOURCES
    src/bootstrap/BootstrapCore.cpp
    src/bootstrap/DllMain.cpp
)
set(BOOTSTRAP_RESOURCE ${RESOURCES_DIR}/bootstrap.rc)
add_library(bootstrap SHARED ${BOOTSTRAP_SOURCES} ${BOOTSTRAP_RESOURCE})
target_include_directories(bootstrap PRIVATE include)
target_link_libraries(bootstrap
    PRIVATE WIL::WIL
    PRIVATE shared
)

# ── runtime (heavyweight game-modification DLL) ───────────────────────────────

set(RUNTIME_SOURCES
    src/runtime/FovService.cpp
    src/runtime/FpsService.cpp
    src/runtime/HookManager.cpp
    src/runtime/InputSampler.cpp
    src/runtime/MemoryResolver.cpp
    src/runtime/RuntimeMain.cpp
    src/runtime/RuntimeState.cpp
)
set(RUNTIME_RESOURCE ${RESOURCES_DIR}/runtime.rc)
add_library(runtime SHARED ${RUNTIME_SOURCES} ${RUNTIME_RESOURCE})
target_include_directories(runtime PRIVATE include)
target_link_libraries(runtime
    PRIVATE mmh::mmh
    PRIVATE WIL::WIL
    PRIVATE shared
    PRIVATE util
)
