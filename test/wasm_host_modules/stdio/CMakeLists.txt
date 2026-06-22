# =============================================================================
# test/wasm_host_modules/stdio/CMakeLists.txt
#   stdio ホストモジュールの単体テストビルド定義。
# =============================================================================

if(BUILD_TESTING)
    set(TEST_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/test_stdio.wit)
    set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/codegen/embwasm_util.py)
    set(GEN_CPP ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.cpp)
    set(GEN_H ${CMAKE_CURRENT_BINARY_DIR}/wasm_api_static.hpp)

    # WIT ファイルの変更を検知して自動生成します
    add_custom_command(
        OUTPUT ${GEN_CPP} ${GEN_H}
        COMMAND python3 ${GEN_SCRIPT} gen-hostapi-dispatch ${TEST_CONFIG_FILE} ${GEN_H} ${GEN_CPP}
        DEPENDS ${TEST_CONFIG_FILE} ${GEN_SCRIPT}
        COMMENT "Generating static API lookup tables for stdio test from test_stdio.wit"
    )

    add_executable(wasm_stdio_tests
        embwasm_hostmodule_stdio_test.cpp
        ${GEN_CPP}
        ${GEN_H}
    )

    target_link_libraries(wasm_stdio_tests
        PRIVATE
        embwasm
        gtest_main
    )

    target_include_directories(wasm_stdio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

    # 警告レベルを高く設定し、警告をエラーとして処理
    if(MSVC)
        target_compile_options(wasm_stdio_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(wasm_stdio_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    # テストの登録
    include(GoogleTest)
    gtest_discover_tests(wasm_stdio_tests)
endif()
