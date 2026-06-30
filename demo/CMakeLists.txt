
add_subdirectory(hello)
add_subdirectory(thread)
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(http)
    add_subdirectory(thread_async)
endif()
add_subdirectory(benchmark)
