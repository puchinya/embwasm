# =============================================================================
# embwasm Core Library CMakeLists.txt
# =============================================================================

# プラットフォーム別ソースコードの選択
if(USE_FREERTOS)
    set(PLATFORM_SRC ../platform/freertos/wasm_platform.cpp)
elseif(USE_UITRON)
    set(PLATFORM_SRC ../platform/uitron/wasm_platform.cpp)
elseif(WIN32)
    set(PLATFORM_SRC ../platform/windows/wasm_platform.cpp)
elseif(APPLE)
    set(PLATFORM_SRC ../platform/macos/wasm_platform.cpp)
else()
    set(PLATFORM_SRC ../platform/macos/wasm_platform.cpp)
endif()

# コアライブラリの定義 (自動生成された GEN_CPP は除外)
add_library(embwasm
    wasm_memory_pool.cpp
    wasm_engine.cpp
    wasm_thread.cpp
    hostmodule/threads/embwasm_hostmodule_thread.cpp
    ${PLATFORM_SRC}
)

# インクルードディレクトリの公開 (PUBLIC) 設定
target_include_directories(embwasm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/hostmodule/threads
)

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm PRIVATE -O2 /W4 /WX)
else()
    target_compile_options(embwasm PRIVATE -O2 -Wall -Wextra -Wpedantic -Werror -fstack-usage)
endif()

# 静的スタックサイズ解析ターゲット (cmake --build . --target embwasm_analyze_stack)
add_custom_target(embwasm_analyze_stack
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/analyze_wasm_stack.py
            --label "libembwasm host"
            --dir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/embwasm.dir
    DEPENDS embwasm
    COMMENT "Analyzing libembwasm static stack usage"
)
