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
    ${PLATFORM_SRC}
)

# インクルードディレクトリの公開 (PUBLIC) 設定
# これによりリンク元ターゲット（demoやtest）に自動でインクルードパスが伝播します。
target_include_directories(embwasm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
