set(CM3_DIR      ${CMAKE_CURRENT_SOURCE_DIR})
set(FREERTOS_DIR ${CM3_DIR}/freertos)

add_library(freertos STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)
target_include_directories(freertos PUBLIC
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${CM3_DIR}
)

add_library(embwasm_cm3_bsp STATIC
    ${CM3_DIR}/startup.c
    ${CM3_DIR}/syscalls.c
    ${CM3_DIR}/uart.c
    ${CM3_DIR}/freertos_start.c
)
target_include_directories(embwasm_cm3_bsp PUBLIC ${CM3_DIR})
target_link_libraries(embwasm_cm3_bsp PUBLIC freertos)

include(${CM3_DIR}/cmake/cm3_freertos.cmake)
