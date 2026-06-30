# Clang cross-compilation toolchain for ARM Cortex-M3
# Uses Arm LLVM Embedded Toolchain for Arm (picolibc + libc++ + compiler-rt)
# https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Override via cmake -DLLVM_ET=/path/to/toolchain
if(NOT DEFINED LLVM_ET)
    set(LLVM_ET /Applications/LLVM-ET-Arm-19.1.5-Darwin-universal)
endif()

# armv7m, soft-float, no FP, no OS syscalls (we provide write/_exit)
set(MULTILIB armv7m_soft_nofp)
set(RUNTIME_DIR ${LLVM_ET}/lib/clang-runtimes/arm-none-eabi/${MULTILIB})

set(CMAKE_C_COMPILER   ${LLVM_ET}/bin/clang       CACHE STRING "")
set(CMAKE_CXX_COMPILER ${LLVM_ET}/bin/clang++     CACHE STRING "")
set(CMAKE_AR           ${LLVM_ET}/bin/llvm-ar     CACHE STRING "")
set(CMAKE_RANLIB       ${LLVM_ET}/bin/llvm-ranlib CACHE STRING "")
set(CMAKE_OBJCOPY      ${LLVM_ET}/bin/llvm-objcopy CACHE STRING "")

set(CPU_FLAGS     "--target=armv7m-none-eabi -mcpu=cortex-m3 -mthumb")
set(SYSROOT_FLAGS "--sysroot=${RUNTIME_DIR}")

set(COMMON_FLAGS
    "${CPU_FLAGS} ${SYSROOT_FLAGS} \
    -Os -g \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-rtti \
    -fno-use-cxa-atexit \
    -Wall -Wno-unused-parameter")

set(CMAKE_C_FLAGS   "${COMMON_FLAGS} -std=c11"       CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -std=c++17"     CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS
    "${CPU_FLAGS} ${SYSROOT_FLAGS} \
    -Wl,--gc-sections \
    -fuse-ld=lld \
    -nostartfiles \
    -rtlib=compiler-rt \
    --unwindlib=none \
    -Wl,--start-group -lc -lm -lc++ -lc++abi -ldummyhost -Wl,--end-group"
    CACHE STRING "" FORCE)
