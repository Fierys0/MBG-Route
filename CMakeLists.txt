cmake_minimum_required(VERSION 3.16)

project(MBG VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING
      "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Build info generation (per build) - matches AfterBloom
add_custom_target(
    generate_buildinfo ALL
    COMMAND ${CMAKE_COMMAND}
            -P ${CMAKE_SOURCE_DIR}/cmake/buildinfo.cmake
)

# Enable Position Independent Code for all targets (required for shared libraries)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Raylib - build as static library to combine with fumbo
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Raylib as a static library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(engine/lib/raylib)

# Set FUMBO_WITH_VIDEO to OFF in the project CMake
set(FUMBO_WITH_VIDEO OFF CACHE BOOL "Disable video support" FORCE)
add_subdirectory(engine)

# Set output directory for binary, dynamic libraries, and assets
set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)

# Set fumbo_engine dynamic library output directory (libfumbo.so on Linux / libfumbo.dll on Windows)
set_target_properties(fumbo_engine PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
  RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
  ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}
)

file(GLOB_RECURSE GAME_SOURCES src/*.cpp)
add_executable(${PROJECT_NAME} ${GAME_SOURCES})

# Put executable in "bin" folder
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
)

# Set RPATH so executable can find fumbo shared library (libfumbo.so) in the same directory
if(UNIX AND NOT APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# Make sure buildinfo is generated before compiling
add_dependencies(${PROJECT_NAME} generate_buildinfo)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        src
        engine
        engine/lib/raylib/src
        ${CMAKE_BINARY_DIR}/cmake   # for generated_buildinfo.hpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        fumbo_engine
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl rt X11)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm gdi32 opengl32 shell32)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--allow-multiple-definition")
endif()

# ===== Asset Packing Tool =====
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(pack_assets engine/tools/pack_assets.cpp)
    target_include_directories(pack_assets PRIVATE engine engine/lib/raylib/src)
    target_compile_features(pack_assets PRIVATE cxx_std_17)
    set_target_properties(pack_assets PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

    add_custom_target(pack_data
        COMMAND pack_assets ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/data.fpk
        DEPENDS pack_assets
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Packing assets into encrypted data.fpk..."
    )
else()
    # When cross-compiling, use host pack_assets binary from Linux build
    add_custom_target(pack_data
        COMMAND ${CMAKE_SOURCE_DIR}/build/bin/pack_assets ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/data.fpk
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Packing assets into encrypted data.fpk using host tool..."
    )
endif()

# Make sure main executable depends on packed assets
add_dependencies(${PROJECT_NAME} pack_data)

# Copy data.fpk and assets to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/data.fpk
        ${OUTPUT_DIR}/data.fpk
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${OUTPUT_DIR}/assets
    COMMENT "Copying data.fpk and assets to binary directory..."
)
