add_library(demo_thread_compat STATIC)

# Include path: consumers use #include "thread_compat/thread_compat.hpp"
target_include_directories(demo_thread_compat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_CROSSCOMPILING)
    target_sources(demo_thread_compat PRIVATE thread_compat_freertos.cpp)
    target_link_libraries(demo_thread_compat PUBLIC freertos)
else()
    target_sources(demo_thread_compat PRIVATE thread_compat_std.cpp)
endif()
