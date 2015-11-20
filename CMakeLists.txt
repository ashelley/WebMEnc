cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

project(WebMEnc)

find_package(DevIL REQUIRED)
include_directories("${IL_INCLUDE_DIR}")

find_path(VPX_INCLUDE_DIR vpx_codec.h REQUIRED)
include_directories("${VPX_INCLUDE_DIR}")
find_library(VPX_LIBRARY REQUIRED)

add_executable(WebMEnc WebMEnc.cpp EbmlWriter.h EbmlWriter.cpp)
target_link_libraries(WebMEnc "${IL_LIBRARIES}" "${ILU_LIBRARIES}" "${VPX_LIBRARY}")
