# =============================================================================
# embwasm Benchmark Demo CMakeLists.txt
# =============================================================================

set(DEMO_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/hostapi.wit)
set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/embwasm_util.py)
set(GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
set(GEN_H ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)
set(GEN_WASM_WIT_DIR ${CMAKE_CURRENT_BINARY_DIR}/wit)
set(GEN_WASM_WIT ${GEN_WASM_WIT_DIR}/wasm_host_api.wit)
set(GEN_WASM_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm_gen)
set(GEN_WASM_H ${GEN_WASM_DIR}/wasm_host_api.h)
set(GEN_WASM_C ${GEN_WASM_DIR}/wasm_host_api.c)

add_custom_command(
    OUTPUT ${GEN_CPP} ${GEN_H}
    COMMAND python3 ${GEN_SCRIPT} gen-hostapi-dispatch ${DEMO_CONFIG_FILE} ${GEN_H} ${GEN_CPP}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_SCRIPT}
    COMMENT "Generating static API lookup tables from hostapi.wit"
)

add_custom_command(
    OUTPUT ${GEN_WASM_H} ${GEN_WASM_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_WASM_WIT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${DEMO_CONFIG_FILE} ${GEN_WASM_WIT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../../wasm_host_modules ${GEN_WASM_WIT_DIR}/deps
    COMMAND wit-bindgen c --out-dir ${GEN_WASM_DIR} --world wasm-host-api ${GEN_WASM_WIT_DIR}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_CPP}
    COMMENT "Generating WASM client bindings using wit-bindgen with deps"
)

find_program(WASM_CLANG
    NAMES clang
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
    NO_DEFAULT_PATH
)
if(NOT WASM_CLANG)
    find_program(WASM_CLANG clang)
endif()

find_program(WASM_LD
    NAMES wasm-ld
    PATHS /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/lld@20/bin /opt/homebrew/opt/lld/bin /usr/local/opt/llvm/bin /usr/local/opt/lld/bin /usr/local/opt/lld@20/bin
    NO_DEFAULT_PATH
)
if(NOT WASM_LD)
    find_program(WASM_LD wasm-ld)
endif()

if(WASM_LD)
    get_filename_component(WASM_LD_DIR ${WASM_LD} DIRECTORY)
    set(WASM_LD_FLAGS "-B${WASM_LD_DIR}/")
else()
    set(WASM_LD_FLAGS "")
endif()

set(WASM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/wasm/bench.c)
set(WASM_OUT ${CMAKE_CURRENT_BINARY_DIR}/bench.wasm)
set(WASM_GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/bench_wasm.cpp)
set(WASM_GEN_H ${CMAKE_CURRENT_BINARY_DIR}/bench_wasm.hpp)

set(WASM_COMMON_INC ${CMAKE_CURRENT_SOURCE_DIR}/../common/wasm/include)
set(WASM_COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../common/wasm/stdlib.c)

set(WASM_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm_objs)
file(MAKE_DIRECTORY ${WASM_OBJ_DIR})

# コンパイル: bench.c → bench.o (-fstack-usage で静的スタック解析用 .su を生成)
add_custom_command(
    OUTPUT ${WASM_OBJ_DIR}/bench.o
    COMMAND ${WASM_CLANG} --target=wasm32 -O3 -nostdlib -fstack-usage -c
            -o ${WASM_OBJ_DIR}/bench.o
            ${WASM_SRC}
            -I${GEN_WASM_DIR} -I${WASM_COMMON_INC}
    DEPENDS ${WASM_SRC} ${GEN_WASM_H}
    COMMENT "Compiling bench.c to WASM object"
)

# コンパイル: stdlib.c → stdlib.o
add_custom_command(
    OUTPUT ${WASM_OBJ_DIR}/stdlib.o
    COMMAND ${WASM_CLANG} --target=wasm32 -O3 -nostdlib -fstack-usage -c
            -o ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_COMMON_SRC}
            -I${WASM_COMMON_INC}
    DEPENDS ${WASM_COMMON_SRC}
    COMMENT "Compiling stdlib.c to WASM object"
)

# コンパイル: wasm_host_api.c → wasm_host_api.o
add_custom_command(
    OUTPUT ${WASM_OBJ_DIR}/wasm_host_api.o
    COMMAND ${WASM_CLANG} --target=wasm32 -O3 -nostdlib -fstack-usage -c
            -o ${WASM_OBJ_DIR}/wasm_host_api.o
            ${GEN_WASM_C}
            -I${GEN_WASM_DIR} -I${WASM_COMMON_INC}
    DEPENDS ${GEN_WASM_H} ${GEN_WASM_C}
    COMMENT "Compiling wasm_host_api.c to WASM object"
)

# リンク: .o ファイル群 → bench.wasm
add_custom_command(
    OUTPUT ${WASM_OUT}
    COMMAND ${WASM_CLANG} --target=wasm32 -nostdlib ${WASM_LD_FLAGS}
            -Wl,--no-entry
            -Wl,--export=fibonacci
            -Wl,--export=sum_loop
            -Wl,--export=count_primes
            -Wl,--export=mandelbrot
            -Wl,--export=matrix_mul
            -Wl,--export=crc32
            -Wl,-z,stack-size=1024
            -Wl,--stack-first
            -Wl,--gc-sections
            -o ${WASM_OUT}
            ${WASM_OBJ_DIR}/bench.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    DEPENDS ${WASM_OBJ_DIR}/bench.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    COMMENT "Linking WASM objects to bench.wasm"
)

set(WASM_TO_CPP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/wasm_to_cpp.py)
add_custom_command(
    OUTPUT ${WASM_GEN_CPP} ${WASM_GEN_H}
    COMMAND python3 ${WASM_TO_CPP_SCRIPT} ${WASM_OUT} ${WASM_GEN_CPP} ${WASM_GEN_H}
    DEPENDS ${WASM_OUT} ${WASM_TO_CPP_SCRIPT}
    COMMENT "Generating C++ source and header from bench.wasm"
)

add_executable(embwasm_demo_benchmark
    main.cpp host_apis.cpp
    ${GEN_CPP} ${GEN_H}
    ${WASM_GEN_CPP} ${WASM_GEN_H}
)

target_link_libraries(embwasm_demo_benchmark PRIVATE embwasm)
target_include_directories(embwasm_demo_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(MSVC)
    target_compile_options(embwasm_demo_benchmark PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_demo_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror -fstack-usage)
endif()

# WASM 側 静的スタックサイズ解析ターゲット
add_custom_target(embwasm_demo_benchmark_analyze_stack
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/analyze_wasm_stack.py
            --label "benchmark WASM"
            ${WASM_OBJ_DIR}/bench.su
            ${WASM_OBJ_DIR}/stdlib.su
            ${WASM_OBJ_DIR}/wasm_host_api.su
    DEPENDS ${WASM_OBJ_DIR}/bench.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    COMMENT "Analyzing WASM static stack usage for benchmark demo"
)

# ホスト側 静的スタックサイズ解析ターゲット
add_custom_target(embwasm_demo_benchmark_host_analyze_stack
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/analyze_wasm_stack.py
            --label "benchmark host"
            --dir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/embwasm_demo_benchmark.dir
    DEPENDS embwasm_demo_benchmark
    COMMENT "Analyzing host-side C++ stack usage for benchmark demo"
)
