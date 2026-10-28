cmake_minimum_required(VERSION 3.10)
project(MBGRouting)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mbg_routing
    src/main.cpp
    src/Graph.cpp
    src/Dijkstra.cpp
    src/DPBaseline.cpp
    src/DPModified.cpp
)

target_include_directories(mbg_routing PRIVATE include)
