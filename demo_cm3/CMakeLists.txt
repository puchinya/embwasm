cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-clang-toolchain.cmake"
        CACHE STRING "Toolchain file")
endif()

project(embwasm_cm3_bench C CXX)

# Project-specific linker flags appended to the toolchain baseline
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} \
    -T${CMAKE_CURRENT_SOURCE_DIR}/link.ld \
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/bench.map"
    CACHE STRING "" FORCE)

set(EMBWASM_ROOT    ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(HOSTMODULE_ROOT ${EMBWASM_ROOT}/wasm_host_modules/src)

# embwasm ライブラリ
file(GLOB EMBWASM_SRCS ${EMBWASM_ROOT}/src/*.cpp)
add_library(embwasm STATIC ${EMBWASM_SRCS})
target_include_directories(embwasm PUBLIC ${EMBWASM_ROOT}/include)
target_compile_definitions(embwasm PRIVATE EMBWASM_ENABLE_MULTITHREADING=0)

# FreeRTOS
set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/freertos)
add_library(freertos STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)
target_include_directories(freertos PUBLIC
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# wasm_api_static.hpp/cpp from hostapi.wit
set(DEMO_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/hostapi.wit)
set(GEN_SCRIPT       ${EMBWASM_ROOT}/tools/codegen/embwasm_util.py)
set(GEN_CPP          ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
set(GEN_H            ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)
add_custom_command(
    OUTPUT ${GEN_CPP} ${GEN_H}
    COMMAND python3 ${GEN_SCRIPT} gen-hostapi-dispatch ${DEMO_CONFIG_FILE} ${GEN_H} ${GEN_CPP}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_SCRIPT}
    COMMENT "Generating wasm_api_static from hostapi.wit"
)

# bench.wasm → bench_wasm.cpp / bench_wasm.hpp
set(BENCH_WASM     ${EMBWASM_ROOT}/build/demo/benchmark/bench.wasm)
set(WASM_TO_CPP    ${EMBWASM_ROOT}/tools/codegen/wasm_to_cpp.py)
set(BENCH_WASM_CPP ${CMAKE_CURRENT_BINARY_DIR}/bench_wasm.cpp)
set(BENCH_WASM_HPP ${CMAKE_CURRENT_BINARY_DIR}/bench_wasm.hpp)
add_custom_command(
    OUTPUT ${BENCH_WASM_CPP} ${BENCH_WASM_HPP}
    COMMAND python3 ${WASM_TO_CPP} ${BENCH_WASM} ${BENCH_WASM_CPP} ${BENCH_WASM_HPP}
    DEPENDS ${BENCH_WASM} ${WASM_TO_CPP}
    COMMENT "bench.wasm -> bench_wasm.cpp / bench_wasm.hpp"
)

# ファームウェア ELF
add_executable(bench_cm3.elf
    startup.c
    syscalls.c
    uart.c
    main.c
    bench_main.cpp
    ${EMBWASM_ROOT}/platform/freertos/wasm_platform.cpp
    ${GEN_CPP}
    ${BENCH_WASM_CPP}
    ${HOSTMODULE_ROOT}/sys_rt/stdio/embwasm_hostmodule_stdio.cpp
    ${HOSTMODULE_ROOT}/sys_rt/system/embwasm_hostmodule_system.cpp
)
target_include_directories(bench_cm3.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${EMBWASM_ROOT}/include
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${EMBWASM_ROOT}/platform/freertos
    ${HOSTMODULE_ROOT}/sys_rt/stdio
    ${HOSTMODULE_ROOT}/sys_rt/system
)
target_compile_definitions(bench_cm3.elf PRIVATE
    EMBWASM_ENABLE_MULTITHREADING=0
)
target_link_libraries(bench_cm3.elf PRIVATE embwasm freertos)

add_custom_command(TARGET bench_cm3.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary bench_cm3.elf bench_cm3.bin
    COMMENT "-> bench_cm3.bin"
)

# Renode simulation
find_program(RENODE_EXECUTABLE
    NAMES renode
    HINTS
        "$ENV{HOME}/Applications/Renode.app/Contents/MacOS"
        "/Applications/Renode.app/Contents/MacOS"
        "/opt/renode"
)
if(RENODE_EXECUTABLE)
    add_custom_target(run_renode
        COMMAND ${RENODE_EXECUTABLE} --plain --console
                ${CMAKE_CURRENT_SOURCE_DIR}/renode/run_bench.resc
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        DEPENDS bench_cm3.elf
        COMMENT "Running bench_cm3 in Renode..."
        USES_TERMINAL
    )
else()
    message(STATUS "Renode not found; 'run_renode' target unavailable")
endif()
