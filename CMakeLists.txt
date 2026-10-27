cmake_minimum_required(VERSION 3.16)

project(MBG VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Raylib
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Raylib as static" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(engine/lib/raylib)

# Fumbo Engine
set(FUMBO_WITH_VIDEO OFF CACHE BOOL "Disable video support" FORCE)
add_subdirectory(engine)

file(GLOB_RECURSE GAME_SOURCES src/*.cpp)
add_executable(${PROJECT_NAME} ${GAME_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    engine
    engine/lib/raylib/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    fumbo_engine
)

if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl rt X11)
endif()

# Symlink assets
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/bin/assets
    COMMENT "Symlinking assets to binary directory..."
)
