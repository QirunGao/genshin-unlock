cmake_minimum_required(VERSION 3.28)
project(genshin-fov-unlock
    VERSION 7.0.0.0
)

set(CMAKE_CXX_STANDARD 23)

if (NOT MSVC)
    message(FATAL_ERROR "This project requires MSVC to build.")
endif()

add_compile_options(
    /permissive-
    /utf-8
    "$<$<CONFIG:Release>:/GL>"
    "$<$<CONFIG:Release>:/Gw>"
)
add_link_options(
    "$<$<CONFIG:Release>:/LTCG>"
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

function(configure_resource target_name description original_filename filetype wrapper_template)
    set(VER_FILEDESCRIPTION ${description})
    set(VER_INTERNALNAME ${target_name})
    set(VER_ORIGINALFILENAME ${original_filename})
    set(VER_FILETYPE ${filetype})
    set(VERSION_RESOURCE "${target_name}.version.rc")
    configure_file(
        res/version.rc.in
        ${RESOURCES_DIR}/${VERSION_RESOURCE}
        @ONLY
    )
    configure_file(
        ${wrapper_template}
        ${RESOURCES_DIR}/${target_name}.rc
        @ONLY
    )
endfunction()

set(LOADER_NAME GenshinUnlockLoader)
set(HELPER_NAME GenshinUnlockElevatedLauncher)
set(PLUGIN_NAME GenshinUnlockPlugin)

configure_resource(
    ${LOADER_NAME}
    "Genshin Unlock Loader"
    "${LOADER_NAME}.exe"
    VFT_APP
    res/loader.rc.in
)
configure_resource(
    ${HELPER_NAME}
    "Genshin Unlock Elevated Launcher"
    "${HELPER_NAME}.exe"
    VFT_APP
    res/loader.rc.in
)
configure_resource(
    ${PLUGIN_NAME}
    "Genshin Unlock Plugin"
    "${PLUGIN_NAME}.dll"
    VFT_DLL
    res/plugin.rc.in
)

add_compile_definitions(WIN32_LEAN_AND_MEAN)

file(GLOB_RECURSE UTIL_SOURCES src/util/*)
add_library(util STATIC ${UTIL_SOURCES})
target_include_directories(util PRIVATE include)
target_link_libraries(util
    PRIVATE version.lib
    PUBLIC WIL::WIL
)

file(GLOB_RECURSE LOADER_SOURCES src/loader/*)
set(LOADER_RESOURCE ${RESOURCES_DIR}/${LOADER_NAME}.rc)
add_executable(loader ${LOADER_SOURCES} ${LOADER_RESOURCE})
set_target_properties(loader PROPERTIES OUTPUT_NAME ${LOADER_NAME})
target_include_directories(loader PRIVATE include)
target_link_libraries(loader
    PRIVATE advapi32
    PRIVATE glaze::glaze
    PRIVATE ole32
    PRIVATE WIL::WIL
    PRIVATE util
)

file(GLOB_RECURSE PLUGIN_SOURCES src/plugin/*)
set(PLUGIN_RESOURCE ${RESOURCES_DIR}/${PLUGIN_NAME}.rc)
add_library(plugin SHARED ${PLUGIN_SOURCES} ${PLUGIN_RESOURCE})
set_target_properties(plugin PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
target_include_directories(plugin PRIVATE include)
target_link_libraries(plugin
    PRIVATE glaze::glaze
    PRIVATE mmh::mmh
    PRIVATE WIL::WIL
    PRIVATE util
)

set(PLUGIN_HASH_HEADER ${CMAKE_CURRENT_BINARY_DIR}/include/common/PluginHash.hpp)
add_custom_command(
    OUTPUT ${PLUGIN_HASH_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DPLUGIN_PATH=$<TARGET_FILE:plugin>
        -DOUTPUT_PATH=${PLUGIN_HASH_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GeneratePluginHash.cmake
    DEPENDS plugin
    VERBATIM
)
add_custom_target(plugin_hash DEPENDS ${PLUGIN_HASH_HEADER})

set(HELPER_SOURCES
    src/helper/Main.cpp
    src/loader/LaunchPlan.cpp
)
set(HELPER_RESOURCE ${RESOURCES_DIR}/${HELPER_NAME}.rc)
add_executable(helper ${HELPER_SOURCES} ${HELPER_RESOURCE})
set_target_properties(helper PROPERTIES OUTPUT_NAME ${HELPER_NAME})
add_dependencies(helper plugin_hash)
target_include_directories(helper PRIVATE include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(helper
    PRIVATE bcrypt
    PRIVATE glaze::glaze
    PRIVATE wintrust
    PRIVATE WIL::WIL
    PRIVATE util
)
target_link_options(helper PRIVATE
    /MANIFESTUAC:level='requireAdministrator'
)
