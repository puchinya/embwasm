# =============================================================================
# embwasm Demo CMakeLists.txt
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

# WIT ファイルの変更を検知して自動生成します (ホスト側 API ルックアップテーブルの生成)
add_custom_command(
    OUTPUT ${GEN_CPP} ${GEN_H}
    COMMAND python3 ${GEN_SCRIPT} gen-hostapi-dispatch ${DEMO_CONFIG_FILE} ${GEN_H} ${GEN_CPP}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_SCRIPT}
    COMMENT "Generating static API lookup tables from hostapi.wit"
)

# wit-bindgen で WASM クライアント用ヘッダー/ソースを生成 (deps ディレクトリ構成を使用)
add_custom_command(
    OUTPUT ${GEN_WASM_H} ${GEN_WASM_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_WASM_WIT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${DEMO_CONFIG_FILE} ${GEN_WASM_WIT}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${GEN_WASM_WIT_DIR}/deps
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../../wasm_host_modules/wit ${GEN_WASM_WIT_DIR}/deps
    COMMAND wit-bindgen c --out-dir ${GEN_WASM_DIR} --world wasm-host-api ${GEN_WASM_WIT_DIR}
    DEPENDS ${DEMO_CONFIG_FILE} ${GEN_CPP}
    COMMENT "Generating WASM client bindings using wit-bindgen with deps"
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

set(WASM_COMMON_INC ${CMAKE_CURRENT_SOURCE_DIR}/../common/wasm/include)
set(WASM_COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../common/wasm/stdlib.c)

set(WASM_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm_objs)
file(MAKE_DIRECTORY ${WASM_OBJ_DIR})

# コンパイル: main.c → main.o (-fstack-usage で静的スタック解析用 .su を生成)
add_custom_command(
    OUTPUT ${WASM_OBJ_DIR}/main.o
    COMMAND ${WASM_CLANG} --target=wasm32 -O3 -nostdlib -fstack-usage -c
            -o ${WASM_OBJ_DIR}/main.o
            ${WASM_SRC}
            -I${GEN_WASM_DIR} -I${WASM_COMMON_INC}
    DEPENDS ${WASM_SRC} ${GEN_WASM_H}
    COMMENT "Compiling main.c to WASM object"
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

# リンク: .o ファイル群 → main.wasm
add_custom_command(
    OUTPUT ${WASM_OUT}
    COMMAND ${WASM_CLANG} --target=wasm32 -nostdlib ${WASM_LD_FLAGS}
            -Wl,--no-entry -Wl,--export=main
            -Wl,-z,stack-size=1024 -Wl,--stack-first
            -o ${WASM_OUT}
            ${WASM_OBJ_DIR}/main.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    DEPENDS ${WASM_OBJ_DIR}/main.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    COMMENT "Linking WASM objects to main.wasm"
)


# 2. WASM バイナリから .cpp (実データ) と .hpp (extern 宣言のみ) を生成するカスタムコマンド
set(WASM_TO_CPP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/codegen/wasm_to_cpp.py)
add_custom_command(
    OUTPUT ${WASM_GEN_CPP} ${WASM_GEN_H}
    COMMAND python3 ${WASM_TO_CPP_SCRIPT} ${WASM_OUT} ${WASM_GEN_CPP} ${WASM_GEN_H}
    DEPENDS ${WASM_OUT} ${WASM_TO_CPP_SCRIPT}
    COMMENT "Generating C++ source and header from main.wasm"
)

add_executable(embwasm_demo_hello main.cpp host_apis.cpp ${GEN_CPP} ${GEN_H} ${WASM_GEN_CPP} ${WASM_GEN_H})

# ライブラリへのリンクおよびインクルードパスの設定
target_link_libraries(embwasm_demo_hello PRIVATE embwasm)
target_include_directories(embwasm_demo_hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm_demo_hello PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_demo_hello PRIVATE -Wall -Wextra -Wpedantic -Werror -fstack-usage)
endif()

# WASM 側 静的スタックサイズ解析ターゲット (cmake --build . --target embwasm_demo_hello_analyze_stack)
add_custom_target(embwasm_demo_hello_analyze_stack
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/analyze_wasm_stack.py
            --label "hello WASM"
            ${WASM_OBJ_DIR}/main.su
            ${WASM_OBJ_DIR}/stdlib.su
            ${WASM_OBJ_DIR}/wasm_host_api.su
    DEPENDS ${WASM_OBJ_DIR}/main.o
            ${WASM_OBJ_DIR}/stdlib.o
            ${WASM_OBJ_DIR}/wasm_host_api.o
    COMMENT "Analyzing WASM static stack usage for hello demo"
)

# ホスト側 静的スタックサイズ解析ターゲット
add_custom_target(embwasm_demo_hello_host_analyze_stack
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/analyze_wasm_stack.py
            --label "hello host"
            --dir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/embwasm_demo_hello.dir
    DEPENDS embwasm_demo_hello
    COMMENT "Analyzing host-side C++ stack usage for hello demo"
)
