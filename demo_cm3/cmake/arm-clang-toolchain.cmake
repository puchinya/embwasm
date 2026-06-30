# Clang cross-compilation toolchain for ARM Cortex-M3 (arm-none-eabi)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TRIPLE arm-none-eabi)
set(GCC_BASE /opt/homebrew/Cellar/arm-none-eabi-gcc/10.3-2021.10/gcc)
set(GCC_VER  10.3.1)

# arm-none-eabi sysroot and library directories
set(ARM_SYSROOT  ${GCC_BASE}/arm-none-eabi)
set(ARM_LIBDIR   ${ARM_SYSROOT}/lib/thumb/v7-m/nofp)
set(GCC_LIBDIR   ${GCC_BASE}/lib/gcc/arm-none-eabi/${GCC_VER}/thumb/v7-m/nofp)
set(CXX_INC1     ${ARM_SYSROOT}/include/c++/${GCC_VER})
set(CXX_INC2     ${ARM_SYSROOT}/include/c++/${GCC_VER}/arm-none-eabi/thumb/v7-m/nofp)

# Homebrew LLVM toolchain
set(LLVM_BIN /opt/homebrew/bin)
set(CMAKE_C_COMPILER   ${LLVM_BIN}/clang       CACHE STRING "")
set(CMAKE_CXX_COMPILER ${LLVM_BIN}/clang++     CACHE STRING "")
set(CMAKE_AR           ${LLVM_BIN}/llvm-ar     CACHE STRING "")
set(CMAKE_RANLIB       ${LLVM_BIN}/llvm-ranlib CACHE STRING "")
set(CMAKE_OBJCOPY      ${LLVM_BIN}/llvm-objcopy CACHE STRING "")

set(CPU_FLAGS     "--target=${TRIPLE} -mcpu=cortex-m3 -mthumb")
set(SYSROOT_FLAGS "--sysroot=${ARM_SYSROOT}")
set(CXX_STDLIB_FLAGS
    "-stdlib=libstdc++ \
    -isystem${CXX_INC1} \
    -isystem${CXX_INC2}")

set(COMMON_FLAGS
    "${CPU_FLAGS} ${SYSROOT_FLAGS} \
    -Os -g \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-rtti \
    -fno-use-cxa-atexit \
    -Wall -Wno-unused-parameter")

set(CMAKE_C_FLAGS   "${COMMON_FLAGS} -std=c11"                        CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} ${CXX_STDLIB_FLAGS} -std=c++17" CACHE STRING "" FORCE)

# Linker flags common to all targets (project adds -T and -Map separately)
set(CMAKE_EXE_LINKER_FLAGS
    "${CPU_FLAGS} ${SYSROOT_FLAGS} \
    -Wl,--gc-sections \
    -fuse-ld=lld \
    -nostartfiles \
    -L${ARM_LIBDIR} \
    -L${GCC_LIBDIR} \
    -Wl,--start-group -lstdc++ -lc -lm -lgcc -Wl,--end-group \
    -rtlib=libgcc --unwindlib=none"
    CACHE STRING "" FORCE)
