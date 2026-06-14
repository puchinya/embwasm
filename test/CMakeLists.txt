# =============================================================================
# embwasm Tests CMakeLists.txt
# =============================================================================

option(BUILD_TESTING "Build unit tests using GoogleTest" ON)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    # Windows等でMSVC実行時ライブラリ設定の衝突を防ぐオプション
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    set(TEST_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/test_env.wit)
    set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/gen_api.py)
    set(GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
    set(GEN_H ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)

    # WIT ファイルの変更を検知して自動生成します
    add_custom_command(
        OUTPUT ${GEN_CPP} ${GEN_H}
        COMMAND python3 ${GEN_SCRIPT} ${TEST_CONFIG_FILE} ${GEN_CPP} ${GEN_H}
        DEPENDS ${TEST_CONFIG_FILE} ${GEN_SCRIPT}
        COMMENT "Generating static API lookup tables for test from test_env.wit"
    )

    add_executable(wasm_tests
        wasm_memory_pool_test.cpp
        wasm_api_test.cpp
        wasm_platform_test.cpp
        wasm_engine_test.cpp
        wasm_thread_test.cpp
        host_apis.cpp
        ${GEN_CPP}
        ${GEN_H}
    )
    
    # テスト対象ライブラリとGoogleTestのリンク
    target_link_libraries(wasm_tests
        PRIVATE
        embwasm
        gtest_main
    )
    target_include_directories(wasm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

    # 警告レベルを高く設定し、警告をエラーとして処理
    if(MSVC)
        target_compile_options(wasm_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(wasm_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    # テストの登録
    include(GoogleTest)
    gtest_discover_tests(wasm_tests)
endif()
