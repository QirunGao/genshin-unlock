cmake_minimum_required(VERSION 3.28)
project(genshin-unlock
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

# Version resources for launcher.exe
set(VER_FILETYPE "VFT_APP")
set(VER_INTERNALNAME "launcher")
set(VER_ORIGINALFILENAME "launcher.exe")
set(VER_FILEDESCRIPTION "Genshin Impact Game Launcher")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/launcher_version.rc
    @ONLY
)

# Version resources for bootstrap.dll
set(VER_FILETYPE "VFT_DLL")
set(VER_INTERNALNAME "bootstrap")
set(VER_ORIGINALFILENAME "bootstrap.dll")
set(VER_FILEDESCRIPTION "Genshin Impact Bootstrap Module")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/bootstrap_version.rc
    @ONLY
)

# Version resources for runtime.dll
set(VER_FILETYPE "VFT_DLL")
set(VER_INTERNALNAME "runtime")
set(VER_ORIGINALFILENAME "runtime.dll")
set(VER_FILEDESCRIPTION "Genshin Impact Runtime Module")
configure_file(
    ${RESOURCES_DIR}/version.rc.in
    ${RESOURCES_DIR}/runtime_version.rc
    @ONLY
)

add_compile_definitions(WIN32_LEAN_AND_MEAN)

# --- Shared library (header + source) ---
file(GLOB SHARED_SOURCES src/shared/*.cpp)
add_library(shared STATIC ${SHARED_SOURCES})
target_include_directories(shared
    PUBLIC include
    PUBLIC src
)
target_link_libraries(shared
    PUBLIC WIL::WIL
)

# --- Utility library ---
file(GLOB_RECURSE UTIL_SOURCES src/util/*)
add_library(util STATIC ${UTIL_SOURCES})
target_include_directories(util PRIVATE include)
target_link_libraries(util
    PRIVATE version.lib
    PUBLIC WIL::WIL
)

# --- Launcher executable ---
file(GLOB_RECURSE LAUNCHER_SOURCES src/launcher/*)
set(LAUNCHER_RESOURCE ${RESOURCES_DIR}/launcher.rc)
add_executable(launcher ${LAUNCHER_SOURCES} ${LAUNCHER_RESOURCE})
target_include_directories(launcher
    PRIVATE include
    PRIVATE src
)
target_link_libraries(launcher
    PRIVATE glaze::glaze
    PRIVATE WIL::WIL
    PRIVATE util
    PRIVATE shared
)
target_link_options(launcher PRIVATE
    /MANIFEST:NO
)

# --- Bootstrap DLL ---
file(GLOB_RECURSE BOOTSTRAP_SOURCES src/bootstrap/*)
set(BOOTSTRAP_RESOURCE ${RESOURCES_DIR}/bootstrap.rc)
add_library(bootstrap SHARED ${BOOTSTRAP_SOURCES} ${BOOTSTRAP_RESOURCE})
target_include_directories(bootstrap
    PRIVATE include
    PRIVATE src
)
target_link_libraries(bootstrap
    PRIVATE WIL::WIL
    PRIVATE util
    PRIVATE shared
)

# --- Runtime DLL ---
file(GLOB_RECURSE RUNTIME_SOURCES src/runtime/*)
set(RUNTIME_RESOURCE ${RESOURCES_DIR}/runtime.rc)
add_library(runtime SHARED ${RUNTIME_SOURCES} ${RUNTIME_RESOURCE})
target_include_directories(runtime
    PRIVATE include
    PRIVATE src
)
target_link_libraries(runtime
    PRIVATE glaze::glaze
    PRIVATE mmh::mmh
    PRIVATE WIL::WIL
    PRIVATE util
    PRIVATE shared
)
