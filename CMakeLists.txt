cmake_minimum_required(VERSION 3.20)
project(embwasm C CXX)

cmake_policy(SET CMP0079 NEW)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_FREERTOS "Use FreeRTOS platform implementation" OFF)
option(USE_UITRON "Use uITRON platform implementation" OFF)
option(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS "Build WebAssembly official spec tests" OFF)

if(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS)
    add_compile_definitions(EMBWASM_USE_SPEC_CONFIG)
endif()

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(wasm_host_modules/src/sockets)
endif()
add_subdirectory(wasm_host_modules/src/sys_rt/stdio)
add_subdirectory(wasm_host_modules/src/sys_rt/system)

if(USE_FREERTOS)
    add_subdirectory(targets/cm3-freertos)
endif()

add_subdirectory(src)

if(TARGET embwasm_sockets)
    target_link_libraries(embwasm PUBLIC embwasm_sockets)
endif()
if(TARGET embwasm_stdio)
    target_link_libraries(embwasm PUBLIC embwasm_stdio)
endif()
if(TARGET embwasm_system)
    target_link_libraries(embwasm PUBLIC embwasm_system)
endif()
if(TARGET freertos)
    # wasm_platform.cpp が FreeRTOS.h を必要とするため伝播 (CMP0079 NEW)
    target_link_libraries(embwasm PUBLIC freertos)
endif()

add_subdirectory(demo)
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(test)
endif()
