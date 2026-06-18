cmake_minimum_required(VERSION 3.11)
project(embwasm CXX)

cmake_policy(SET CMP0079 NEW)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_FREERTOS "Use FreeRTOS platform implementation" OFF)
option(USE_UITRON "Use uITRON platform implementation" OFF)
option(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS "Build WebAssembly official spec tests" OFF)

if(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS)
    add_compile_definitions(EMBWASM_USE_SPEC_CONFIG)
endif()

add_subdirectory(wasm_host_modules/sockets)
add_subdirectory(wasm_host_modules/stdio)

add_subdirectory(src)

if(TARGET embwasm_sockets)
    target_link_libraries(embwasm PUBLIC embwasm_sockets)
endif()
if(TARGET embwasm_stdio)
    target_link_libraries(embwasm PUBLIC embwasm_stdio)
endif()

add_subdirectory(demo)
add_subdirectory(test)
