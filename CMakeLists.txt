cmake_minimum_required(VERSION 3.11)
project(embwasm CXX)

# 別サブディレクトリで定義されたターゲットへの target_link_libraries を許可する
cmake_policy(SET CMP0079 NEW)

# C++11以上でビルドできるように設定します。
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# プラットフォームビルドオプションおよび公式テストオプションをルートで管理
option(USE_FREERTOS "Use FreeRTOS platform implementation" OFF)
option(USE_UITRON "Use uITRON platform implementation" OFF)
option(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS "Build WebAssembly official spec tests" OFF)

if(EMBWASM_BUILD_OFFICIAL_SPEC_TESTS)
    add_compile_definitions(EMBWASM_USE_SPEC_CONFIG)
endif()

# =============================================================================
# 各モジュール (コアライブラリ、デモ、テスト) の追加
# =============================================================================

# wasi:sockets ホストモジュール
# コアライブラリ (src) とは完全に独立したサブディレクトリとして管理する。
# src/CMakeLists.txt はソケットを一切知らない。
add_subdirectory(wasm_host_modules/sockets)
add_subdirectory(wasm_host_modules/stdio)

add_subdirectory(src)

# embwasm_sockets や embwasm_stdio が有効な場合、コアライブラリにリンクする。
# ルートで行うことで src/CMakeLists.txt をモジュール完全非依存に保つ。
if(TARGET embwasm_sockets)
    target_link_libraries(embwasm PUBLIC embwasm_sockets)
endif()
if(TARGET embwasm_stdio)
    target_link_libraries(embwasm PUBLIC embwasm_stdio)
endif()

add_subdirectory(demo)
add_subdirectory(test)
