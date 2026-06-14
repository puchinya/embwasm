cmake_minimum_required(VERSION 3.11)
project(embwasm CXX)

# C++11以上でビルドできるように設定します。
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# プラットフォームビルドオプションのみルートで管理
option(USE_FREERTOS "Use FreeRTOS platform implementation" OFF)
option(USE_UITRON "Use uITRON platform implementation" OFF)

# =============================================================================
# 各モジュール (コアライブラリ、デモ、テスト) の追加
# =============================================================================
add_subdirectory(src)
add_subdirectory(demo)
add_subdirectory(test)
