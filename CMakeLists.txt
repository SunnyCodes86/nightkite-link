cmake_minimum_required(VERSION 3.16)

# CMake 4 on macOS otherwise resolves the relative ESP-IDF compiler names to
# /usr/bin/cc during the first (cache-free) configure.
find_program(NIGHTKITE_P4_CC riscv32-esp-elf-gcc REQUIRED)
find_program(NIGHTKITE_P4_CXX riscv32-esp-elf-g++ REQUIRED)
set(CMAKE_C_COMPILER "${NIGHTKITE_P4_CC}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${NIGHTKITE_P4_CXX}" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${NIGHTKITE_P4_CC}" CACHE FILEPATH "" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nightkite-link)
