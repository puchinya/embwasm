# =============================================================================
# wasm_host_modules/stdio/CMakeLists.txt
#   wasi:stdio ホストモジュールの独立ビルド定義。
# =============================================================================

option(EMBWASM_ENABLE_STDIO "Enable stdio host module" ON)

if(NOT EMBWASM_ENABLE_STDIO)
    return()
endif()

# =============================================================================
# embwasm_stdio スタティックライブラリの定義
# =============================================================================
add_library(embwasm_stdio STATIC
    embwasm_hostmodule_stdio.cpp
)

# インクルードディレクトリの公開 (PUBLIC) 設定
target_include_directories(embwasm_stdio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# コンパイル定義の公開
target_compile_definitions(embwasm_stdio PUBLIC EMBWASM_ENABLE_STDIO=1)

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm_stdio PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_stdio PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
