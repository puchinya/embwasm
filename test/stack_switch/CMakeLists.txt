# =============================================================================
# stack_switch テスト
# =============================================================================

option(BUILD_TESTING "Build unit tests using GoogleTest" ON)
if(NOT BUILD_TESTING)
    return()
endif()

# ─── WASM コンパイル ─────────────────────────────────────────────────────────

find_program(SS_WASM_CLANG
    NAMES clang
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
    NO_DEFAULT_PATH
)
if(NOT SS_WASM_CLANG)
    find_program(SS_WASM_CLANG clang)
endif()

find_program(SS_WASM_LD
    NAMES wasm-ld
    PATHS /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/lld@20/bin /opt/homebrew/opt/lld/bin
          /usr/local/opt/llvm/bin /usr/local/opt/lld/bin /usr/local/opt/lld@20/bin
    NO_DEFAULT_PATH
)
if(NOT SS_WASM_LD)
    find_program(SS_WASM_LD wasm-ld)
endif()

if(SS_WASM_CLANG AND SS_WASM_LD)
    get_filename_component(SS_WASM_LD_DIR ${SS_WASM_LD} DIRECTORY)
    set(SS_WASM_LD_FLAGS "-B${SS_WASM_LD_DIR}/")
else()
    set(SS_WASM_LD_FLAGS "")
endif()

set(SS_WASM_SRC    ${CMAKE_CURRENT_SOURCE_DIR}/wasm/main.c)
set(SS_WASM_COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../demo/common/wasm/stdlib.c)
set(SS_WASM_COMMON_INC ${CMAKE_CURRENT_SOURCE_DIR}/../../demo/common/wasm/include)
set(SS_WASM_OBJ_DIR    ${CMAKE_CURRENT_BINARY_DIR}/wasm_objs)
set(SS_WASM_OUT        ${CMAKE_CURRENT_BINARY_DIR}/stack_switch.wasm)
set(SS_WASM_GEN_CPP    ${CMAKE_CURRENT_BINARY_DIR}/stack_switch_wasm.cpp)
set(SS_WASM_GEN_H      ${CMAKE_CURRENT_BINARY_DIR}/stack_switch_wasm.hpp)

set(WASM_TO_CPP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/wasm_to_cpp.py)
set(SS_WASM_CFLAGS -mbulk-memory -mmultivalue -msign-ext -mreference-types)

if(SS_WASM_CLANG)
    file(MAKE_DIRECTORY ${SS_WASM_OBJ_DIR})

    add_custom_command(
        OUTPUT ${SS_WASM_OBJ_DIR}/main.o
        COMMAND ${SS_WASM_CLANG} --target=wasm32 -O2 -nostdlib ${SS_WASM_CFLAGS} -c
                -o ${SS_WASM_OBJ_DIR}/main.o ${SS_WASM_SRC}
                -I${SS_WASM_COMMON_INC}
        DEPENDS ${SS_WASM_SRC}
        COMMENT "Compiling stack_switch main.c to WASM object"
    )

    add_custom_command(
        OUTPUT ${SS_WASM_OBJ_DIR}/stdlib.o
        COMMAND ${SS_WASM_CLANG} --target=wasm32 -O2 -nostdlib ${SS_WASM_CFLAGS} -c
                -o ${SS_WASM_OBJ_DIR}/stdlib.o ${SS_WASM_COMMON_SRC}
                -I${SS_WASM_COMMON_INC}
        DEPENDS ${SS_WASM_COMMON_SRC}
        COMMENT "Compiling stack_switch stdlib.c to WASM object"
    )

    add_custom_command(
        OUTPUT ${SS_WASM_OUT}
        COMMAND ${SS_WASM_CLANG} --target=wasm32 -nostdlib ${SS_WASM_LD_FLAGS}
                -Wl,--no-entry
                -Wl,-z,stack-size=4096
                -Wl,--export=thread_worker
                -Wl,--export=cabi_realloc
                -Wl,--export=__stack_pointer
                -Wl,--export=__data_end
                -o ${SS_WASM_OUT}
                ${SS_WASM_OBJ_DIR}/main.o
                ${SS_WASM_OBJ_DIR}/stdlib.o
        DEPENDS ${SS_WASM_OBJ_DIR}/main.o ${SS_WASM_OBJ_DIR}/stdlib.o
        COMMENT "Linking stack_switch WASM"
    )

    add_custom_command(
        OUTPUT ${SS_WASM_GEN_CPP} ${SS_WASM_GEN_H}
        COMMAND python3 ${WASM_TO_CPP_SCRIPT} ${SS_WASM_OUT} ${SS_WASM_GEN_CPP} ${SS_WASM_GEN_H}
        DEPENDS ${SS_WASM_OUT} ${WASM_TO_CPP_SCRIPT}
        COMMENT "Generating C++ source and header from stack_switch.wasm"
    )

    set(SS_WASM_GENERATED ${SS_WASM_GEN_CPP} ${SS_WASM_GEN_H})
    set(SS_HAS_WASM TRUE)
else()
    message(STATUS "stack_switch: LLVM clang not found, skipping WASM compilation")
    set(SS_WASM_GENERATED "")
    set(SS_HAS_WASM FALSE)
endif()

# ─── ホスト API ディスパッチテーブルの生成 ───────────────────────────────────────

set(SS_TEST_WIT    ${CMAKE_CURRENT_SOURCE_DIR}/test_env.wit)
set(SS_GEN_SCRIPT  ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/embwasm_util.py)
set(SS_GEN_CPP     ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
set(SS_GEN_H       ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)

add_custom_command(
    OUTPUT ${SS_GEN_CPP} ${SS_GEN_H}
    COMMAND python3 ${SS_GEN_SCRIPT} gen-hostapi-dispatch ${SS_TEST_WIT} ${SS_GEN_H} ${SS_GEN_CPP}
    DEPENDS ${SS_TEST_WIT} ${SS_GEN_SCRIPT}
    COMMENT "Generating static API lookup tables for stack_switch test"
)

# 空 world の生成コードには unused-parameter / zero-length-array が出るため抑制
set_source_files_properties(${SS_GEN_CPP} PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-zero-length-array"
)

# ─── テスト実行ファイル ────────────────────────────────────────────────────────

add_executable(wasm_stack_switch_tests
    wasm_stack_switch_test.cpp
    host_apis.cpp
    ${SS_GEN_CPP} ${SS_GEN_H}
    ${SS_WASM_GENERATED}
)

target_link_libraries(wasm_stack_switch_tests PRIVATE embwasm gtest_main)
target_include_directories(wasm_stack_switch_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(SS_HAS_WASM)
    target_compile_definitions(wasm_stack_switch_tests PRIVATE SS_HAS_WASM=1)
endif()

if(MSVC)
    target_compile_options(wasm_stack_switch_tests PRIVATE /W4 /WX)
else()
    target_compile_options(wasm_stack_switch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GoogleTest)
gtest_discover_tests(wasm_stack_switch_tests DISCOVERY_MODE PRE_TEST)
