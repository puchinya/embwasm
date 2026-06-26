# =============================================================================
# wasm_host_modules/sockets/CMakeLists.txt
#   wasi:sockets ホストモジュールの独立ビルド定義。
#   このモジュールは embwasm コアライブラリから分離されており、
#   必要に応じて選択してリンクできる。
# =============================================================================

option(EMBWASM_ENABLE_SOCKETS "Enable wasi:sockets host module" ON)
option(USE_LWIP_SOCKETS "Use lwIP 2.x socket platform instead of OS native" OFF)

if(NOT EMBWASM_ENABLE_SOCKETS)
    return()
endif()

# =============================================================================
# プラットフォーム別ソースコードの選択
# =============================================================================
if(USE_LWIP_SOCKETS)
    # lwIP 2.x プラットフォーム実装
    # lwIP のインクルードパスはユーザーが LWIP_INCLUDE_DIRS で指定する。
    set(SOCKET_PLATFORM_SRC platform/lwip/socket_platform.cpp)
    if(DEFINED LWIP_INCLUDE_DIRS)
        set(SOCKET_EXTRA_INCLUDES ${LWIP_INCLUDE_DIRS})
    endif()
elseif(WIN32)
    # Windows Winsock2 プラットフォーム実装
    set(SOCKET_PLATFORM_SRC platform/windows/socket_platform.cpp)
else()
    # macOS / Linux POSIX プラットフォーム実装
    set(SOCKET_PLATFORM_SRC platform/macos/socket_platform.cpp)
endif()

# =============================================================================
# embwasm_sockets スタティックライブラリの定義
# =============================================================================
add_library(embwasm_sockets STATIC
    embwasm_hostmodule_socket.cpp
    ${SOCKET_PLATFORM_SRC}
)

# インクルードディレクトリの公開 (PUBLIC) 設定
# PUBLIC にすることで、リンク先ターゲットに自動でインクルードパスが伝播する。
target_include_directories(embwasm_sockets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${SOCKET_EXTRA_INCLUDES}
)

# コンパイル定義の公開
target_compile_definitions(embwasm_sockets PUBLIC EMBWASM_ENABLE_SOCKETS=1)

# Windows では Winsock2 ライブラリをリンクする
if(WIN32 AND NOT USE_LWIP_SOCKETS)
    target_link_libraries(embwasm_sockets PUBLIC Ws2_32)
endif()

# lwIP ライブラリのリンク（ユーザーが LWIP_LIBRARIES を指定した場合）
if(USE_LWIP_SOCKETS AND DEFINED LWIP_LIBRARIES)
    target_link_libraries(embwasm_sockets PUBLIC ${LWIP_LIBRARIES})
endif()

# 警告レベルを高く設定し、警告をエラーとして処理
if(MSVC)
    target_compile_options(embwasm_sockets PRIVATE /W4 /WX)
else()
    target_compile_options(embwasm_sockets PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
