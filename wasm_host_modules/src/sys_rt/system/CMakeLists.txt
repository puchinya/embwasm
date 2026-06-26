# =============================================================================
# wasm_host_modules/sys_rt/system/CMakeLists.txt
#   sys:rt::system ホストモジュールの独立ビルド定義。
# =============================================================================

option(EMBWASM_ENABLE_SYSTEM "Enable system host module" ON)

if(NOT EMBWASM_ENABLE_SYSTEM)
    return()
endif()

# =============================================================================
# embwasm_system スタティックライブラリの定義
# =============================================================================
add_library(embwasm_system STATIC
    embwasm_hostmodule_system.cpp
)

# インクルードディレクトリの公開 (PUBLIC) 設定
target_include_directories(embwasm_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# コンパイル定義の公開
target_compile_definitions(embwasm_system PUBLIC EMBWASM_ENABLE_SYSTEM=1)

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm_system PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_system PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()