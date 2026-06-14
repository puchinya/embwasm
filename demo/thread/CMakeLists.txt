# =============================================================================
# embwasm Demo CMakeLists.txt
# =============================================================================

set(DEMO_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/module_config.yaml)
set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/gen_api.py)
set(GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
set(GEN_H ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)

# module_config.yaml の変更を検知して自動生成します
add_custom_command(
    OUTPUT ${GEN_CPP} ${GEN_H}
    COMMAND python3 ${GEN_SCRIPT} ${DEMO_CONFIG_FILE} ${GEN_CPP} ${GEN_H}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_SCRIPT}
    COMMENT "Generating static API lookup tables for demo from module_config.yaml"
)

# 1. WebAssembly用の clang を探索 (Homebrewの LLVM などを優先)
find_program(WASM_CLANG
    NAMES clang
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
    NO_DEFAULT_PATH
)
if(NOT WASM_CLANG)
    find_program(WASM_CLANG clang)
endif()

# wasm-ld (WebAssembly用リンカー) を探索 (Homebrewの lld@20 や lld など)
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

set(WASM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/wasm/main.c)
set(WASM_OUT ${CMAKE_CURRENT_BINARY_DIR}/main.wasm)
set(WASM_GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/main_wasm.cpp)
set(WASM_GEN_H ${CMAKE_CURRENT_BINARY_DIR}/main_wasm.hpp)

add_custom_command(
    OUTPUT ${WASM_OUT}
    COMMAND ${WASM_CLANG} --target=wasm32 -O3 -nostdlib ${WASM_LD_FLAGS} -Wl,--no-entry -Wl,--export=main -Wl,--export=thread2 -o ${WASM_OUT} ${WASM_SRC}
    DEPENDS ${WASM_SRC}
    COMMENT "Compiling main.c to WASM using ${WASM_CLANG}"
)

# 2. WASM バイナリから .cpp (実データ) と .hpp (extern 宣言のみ) を生成するカスタムコマンド
set(WASM_TO_CPP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/wasm_to_cpp.py)
add_custom_command(
    OUTPUT ${WASM_GEN_CPP} ${WASM_GEN_H}
    COMMAND python3 ${WASM_TO_CPP_SCRIPT} ${WASM_OUT} ${WASM_GEN_CPP} ${WASM_GEN_H}
    DEPENDS ${WASM_OUT} ${WASM_TO_CPP_SCRIPT}
    COMMENT "Generating C++ source and header from main.wasm"
)

add_executable(embwasm_demo_thread main.cpp host_apis.cpp ${GEN_CPP} ${GEN_H} ${WASM_GEN_CPP} ${WASM_GEN_H})

# ライブラリへのリンクおよびインクルードパスの設定
target_link_libraries(embwasm_demo_thread PRIVATE embwasm)
target_include_directories(embwasm_demo_thread PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm_demo_thread PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_demo_thread PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
