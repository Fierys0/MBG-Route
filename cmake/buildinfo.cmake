string(TIMESTAMP BUILD_TIME "%y%m%d-%H%M")

file(WRITE
    "${CMAKE_BINARY_DIR}/cmake/generated_buildinfo.hpp"
    "#pragma once\n"
    "#define BUILD_TIME \"${BUILD_TIME}\"\n"
)
