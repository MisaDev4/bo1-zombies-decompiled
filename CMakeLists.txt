cmake_minimum_required(VERSION 3.16)
project(BO1Zombies)

set(BIN_NAME "BO1Zombies")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_GENERATOR_PLATFORM "WIN32")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TL_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/tl")
set(DW_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/DemonWare")

include(cmake_files.cmake)

# phys_gjk.cpp specific flag (experimental)
set_source_files_properties(
    "${SRC_DIR}/physics/phys_gjk.cpp"
    PROPERTIES
    COMPILE_OPTIONS "/arch:IA32"
)

# ----- Preprocessor Defines -----
# Common to both Debug and Release
target_compile_definitions(BO1Zombies PUBLIC
    WIN32
    _CONSOLE
    BO1_MP
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_NO_DEPRECATED_NO_WARNINGS
)
# Debug-only
target_compile_definitions(BO1Zombies PUBLIC
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:TRACY_ENABLE>
    $<$<CONFIG:Debug>:TRACY_ON_DEMAND>
)
# Release-only
target_compile_definitions(BO1Zombies PUBLIC
    $<$<CONFIG:Release>:NDEBUG>
)

# ----- Include directories -----
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtomcrypt-1.17/src/headers")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtommath-1.0")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0/include")
# DXSDK_DIR: the June 2010 DirectX SDK root (Include/, Lib/x86/). Defaults to its installer's path;
# pass -DDXSDK_DIR=<dir> to use an extracted copy instead.
set(DXSDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)" CACHE PATH "June 2010 DirectX SDK root")
target_include_directories(BO1Zombies PUBLIC "${DXSDK_DIR}/Include")

# ----- Library directories -----
target_link_directories(BO1Zombies PUBLIC "${DXSDK_DIR}/Lib/x86")
target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0")
target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/steam/redistributable_bin")
target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/binklib")
target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/nvapi")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT  /O2 /MP /W3 ${MSVC_WARNING_DISABLES} /permissive")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /MTd /Od /MP /W3 ${MSVC_WARNING_DISABLES} /permissive")

if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)
    target_link_options(BO1Zombies PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
    target_compile_options(BO1Zombies PRIVATE $<$<CONFIG:Debug>:/ZI>)

    set(CMAKE_CXX_FLAGS_DEBUG           "${CMAKE_CXX_FLAGS_DEBUG} /SAFESEH:NO")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG    "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
endif()

# ----- Links -----
target_link_libraries(BO1Zombies PUBLIC
    steam_api.lib
    binkw32.lib
    nvapi.lib
    d3d9.lib
    ws2_32.lib
    dxguid.lib
    dsound.lib
    winmm.lib
    xinput.lib
    setupapi.lib
    hid.lib
    xapobase.lib
    dxerr.lib
    libvpx.lib
    # June-2010 DirectX SDK libs (xapobase, dxerr) reference pre-VS2015 CRT
    # symbols (_vsnprintf, etc.) that the modern UCRT no longer exports.
    # legacy_stdio_definitions.lib provides shims so those static libs link.
    legacy_stdio_definitions.lib
)
# ----- Debug Links -----
target_link_libraries(BO1Zombies PUBLIC $<$<CONFIG:Debug>:d3dx9d.lib>)
# ----- Release Links -----
target_link_libraries(BO1Zombies PUBLIC $<$<CONFIG:Release>:d3dx9.lib>)
target_link_options(BO1Zombies PRIVATE /machine:x86)

# ----- Linker Options -----
# /DYNAMICBASE:NO - Turning this off causes directx to crash "directx has encountered an unrecoverable error". 
# /LARGEADDRESSAWARE - for convenience (4gb)
# /DEBUG - PDB for both configs 
target_link_options(BO1Zombies PRIVATE /DYNAMICBASE:NO /LARGEADDRESSAWARE /DEBUG)

set_target_properties(BO1Zombies PROPERTIES WIN32_EXECUTABLE TRUE)

# Set the VS debugger's working directory to the build-output dir for whichever config is active 
set_target_properties(BO1Zombies PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# Mods live apart from the base game: mods/<name>/ is copied next to the exe on every build and is only
# loaded with +set fs_game mods/<name> (see mods/sandbox/README.md), or several at once with +set fs_mods "a b c"
# (mods/_stack/maps/_modstack.gsc). Without fs_game / fs_mods nothing in it is read.
add_custom_target(copy_mods ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/mods" "${CMAKE_BINARY_DIR}/$<CONFIG>/mods"
    COMMENT "Copying mods/ to the run folder")

# Base-game data files the install does not carry (data/main/ -> main/ next to the exe), e.g. playlists_sp.info,
# the offline stand-in for the retail online WAD entry (see BG_SetZombieExternalDvars in src/bgame/bg_misc.cpp).
add_custom_target(copy_base_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data/main" "${CMAKE_BINARY_DIR}/$<CONFIG>/main"
    COMMENT "Copying data/main/ to the run folder")

# Keep the build machine's folder out of the binary: source paths in asserts are trimmed to the project and the
# PDB is referenced by file name only.
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" BO1_SOURCE_DIR_NATIVE)
target_compile_options(BO1Zombies PRIVATE "/d1trimfile:${BO1_SOURCE_DIR_NATIVE}")
target_link_options(BO1Zombies PRIVATE "/PDBALTPATH:%_PDB%")
