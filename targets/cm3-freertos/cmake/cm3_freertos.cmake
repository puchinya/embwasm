# embwasm_cm3_freertos_setup_firmware(target
#     [STACK_SIZE bytes]   # APP_TASK_STACK_SIZE (default 4096)
# )
# Handles: BSP link, linker script, .bin generation, Renode target
# Does NOT link embwasm — caller's CMakeLists.txt is responsible
function(embwasm_cm3_freertos_setup_firmware target)
    # CMAKE_CURRENT_FUNCTION_LIST_DIR resolves to this file's directory at call time
    set(_CM3_FREERTOS_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..)
    cmake_parse_arguments(ARG "" "STACK_SIZE" "" ${ARGN})

    set(_stack ${ARG_STACK_SIZE})
    if(NOT _stack)
        set(_stack 4096)
    endif()

    target_compile_definitions(${target} PRIVATE APP_TASK_STACK_SIZE=${_stack})

    set_target_properties(${target} PROPERTIES SUFFIX ".elf")

    target_link_libraries(${target} PRIVATE embwasm_cm3_bsp)

    target_link_options(${target} PRIVATE
        -T${_CM3_FREERTOS_DIR}/link.ld
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${target}> ${CMAKE_CURRENT_BINARY_DIR}/${target}.bin
        COMMENT "-> ${target}.bin"
    )

    set(FIRMWARE_NAME ${target})
    set(ELF_ABSOLUTE_PATH ${CMAKE_CURRENT_BINARY_DIR}/${target}.elf)
    configure_file(
        ${_CM3_FREERTOS_DIR}/renode/run_firmware.resc.in
        ${CMAKE_CURRENT_BINARY_DIR}/run_renode_${target}.resc
        @ONLY
    )

    find_program(RENODE_EXECUTABLE renode
        HINTS
            "$ENV{HOME}/Applications/Renode.app/Contents/MacOS"
            "/Applications/Renode.app/Contents/MacOS"
            "/opt/renode"
    )
    if(RENODE_EXECUTABLE)
        add_custom_target(run_renode_${target}
            COMMAND ${RENODE_EXECUTABLE} --plain --console
                    ${CMAKE_CURRENT_BINARY_DIR}/run_renode_${target}.resc
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS ${target}
            COMMENT "Running ${target} in Renode..."
            USES_TERMINAL
        )
    endif()
endfunction()
