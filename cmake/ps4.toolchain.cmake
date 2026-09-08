# CMake toolchain file for building WoWee for the PlayStation 4 with the
# OpenOrbis PS4 Toolchain (https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).
#
# Usage:
#   export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain   (tools/ps4/fetch-toolchain.sh)
#   cmake -S . -B build-ps4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake
#   cmake --build build-ps4
#
# The flags below are the ones the OpenOrbis samples use: clang targeting
# x86_64-pc-freebsd12-elf against the toolchain's musl + libc++ sysroot, and
# ld.lld with the toolchain's linker script. The final step (create-fself ->
# eboot.bin, PkgTool -> .pkg) is driven from CMakeLists.txt when WOWEE_PS4 is on.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CROSSCOMPILING TRUE)
set(WOWEE_PS4 ON CACHE BOOL "Building for PlayStation 4 (OpenOrbis)" FORCE)

if(NOT DEFINED OO_PS4_TOOLCHAIN)
    if(DEFINED ENV{OO_PS4_TOOLCHAIN})
        set(OO_PS4_TOOLCHAIN "$ENV{OO_PS4_TOOLCHAIN}")
    else()
        message(FATAL_ERROR
            "OO_PS4_TOOLCHAIN is not set. Run tools/ps4/fetch-toolchain.sh or point it at "
            "an OpenOrbis PS4 Toolchain installation.")
    endif()
endif()
set(OO_PS4_TOOLCHAIN "${OO_PS4_TOOLCHAIN}" CACHE PATH "OpenOrbis PS4 Toolchain root")

if(NOT EXISTS "${OO_PS4_TOOLCHAIN}/link.x" OR NOT EXISTS "${OO_PS4_TOOLCHAIN}/lib/crt1.o")
    message(FATAL_ERROR "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN} does not look like an OpenOrbis toolchain (missing link.x or lib/crt1.o)")
endif()

# Host clang/lld. The toolchain ships no compiler; any clang >= 12 works
# (the release used here was built against LLVM 18, so prefer that).
find_program(WOWEE_PS4_CLANG   NAMES clang-18 clang REQUIRED)
find_program(WOWEE_PS4_CLANGXX NAMES clang++-18 clang++ REQUIRED)
find_program(WOWEE_PS4_LLD     NAMES ld.lld-18 ld.lld REQUIRED)
find_program(WOWEE_PS4_AR      NAMES llvm-ar-18 llvm-ar ar REQUIRED)
find_program(WOWEE_PS4_RANLIB  NAMES llvm-ranlib-18 llvm-ranlib ranlib REQUIRED)

set(CMAKE_C_COMPILER   "${WOWEE_PS4_CLANG}")
set(CMAKE_CXX_COMPILER "${WOWEE_PS4_CLANGXX}")
set(CMAKE_AR           "${WOWEE_PS4_AR}")
set(CMAKE_RANLIB       "${WOWEE_PS4_RANLIB}")
set(CMAKE_LINKER       "${WOWEE_PS4_LLD}")

set(CMAKE_C_COMPILER_TARGET   x86_64-pc-freebsd12-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-freebsd12-elf)
set(CMAKE_SYSROOT "${OO_PS4_TOOLCHAIN}")

# try_compile must not attempt to link with the host's default driver logic.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# PS4 uses AMD Jaguar x86-64 cores: SSE4.2 and POPCNT are available, while
# AVX is deliberately excluded. These flags enable Clang's safe loop/vector
# transformations for CPU culling, animation sampling and asset bookkeeping.
set(WOWEE_PS4_COMMON_FLAGS
    "-fPIC -ffunction-sections -fdata-sections -funwind-tables -fexceptions -msse4.2 -mpopcnt -mtune=btver2 -D_GNU_SOURCE -D__ORBIS__ -D__PS4__ -DWOWEE_PS4=1")
set(CMAKE_C_FLAGS_INIT   "${WOWEE_PS4_COMMON_FLAGS} -isystem ${OO_PS4_TOOLCHAIN}/include")
# libc++'s headers must come BEFORE musl's: libc++ wraps <math.h>/<stdlib.h>
# and the C++ overloads (needed by glm) vanish if musl's copies are found first.
set(CMAKE_CXX_FLAGS_INIT "${WOWEE_PS4_COMMON_FLAGS} -fcxx-exceptions -isystem ${OO_PS4_TOOLCHAIN}/include/c++/v1 -isystem ${OO_PS4_TOOLCHAIN}/include")

# Link exactly like the OpenOrbis samples: ld.lld directly, PIE, the toolchain's
# linker script, crt1.o last. Keep <LINK_FLAGS> so target_link_options
# (including the clock_gettime wrapper) reach this direct linker invocation.
set(CMAKE_EXE_LINKER_FLAGS_INIT "")
# <LINK_LIBRARIES> is wrapped in --start-group/--end-group: ps4_vulkan_icd,
# ps4_opengnm and ps4_psbc (cmake/ps4/ps4_vulkan.cmake) cross-reference each
# other's symbols, and ld.lld (unlike a linker that keeps re-scanning) only
# gets one pass over a plain archive list.
#
# --gc-sections was tried here to paper over a handful of undefined symbols
# (Mesa's blake3-based shader-cache hashing and a logging helper missing from
# libpsbc.orbis.a - now provided directly by mesa_stubs.c instead - and
# imgui_impl_vulkan.cpp's unused desktop windowing helpers pulling in the
# VK_KHR_surface queries PS4 has no use for - now stubbed directly in
# vk_ps4_stubs.c instead, see its VK_KHR_surface stubs section). Getting rid
# of it in favor of real stub symbols in our own code is deliberate: PS4's
# create-fself / SELF verification is sensitive to exactly which sections
# ld.lld emits, and CMAKE_C_FLAGS_INIT above still carries
# -ffunction-sections/-fdata-sections for anyone who needs to re-add it.
set(CMAKE_C_LINK_EXECUTABLE
    "${WOWEE_PS4_LLD} -m elf_x86_64 -pie --script ${OO_PS4_TOOLCHAIN}/link.x --eh-frame-hdr --error-limit=0 -L${OO_PS4_TOOLCHAIN}/lib <LINK_FLAGS> <OBJECTS> -o <TARGET> --start-group <LINK_LIBRARIES> --end-group -lc -lkernel ${OO_PS4_TOOLCHAIN}/lib/crt1.o")
set(CMAKE_CXX_LINK_EXECUTABLE
    "${WOWEE_PS4_LLD} -m elf_x86_64 -pie --script ${OO_PS4_TOOLCHAIN}/link.x --eh-frame-hdr --error-limit=0 -L${OO_PS4_TOOLCHAIN}/lib <LINK_FLAGS> <OBJECTS> -o <TARGET> --start-group <LINK_LIBRARIES> --end-group -lc++ -lc -lkernel ${OO_PS4_TOOLCHAIN}/lib/crt1.o")

# No shared libraries on the PS4 side of this build.
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_ROOT_PATH "${OO_PS4_TOOLCHAIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Executables come out as plain PIE ELF; CMakeLists.txt adds the create-fself -> eboot.bin step.
# (CMake resets CMAKE_EXECUTABLE_SUFFIX for Generic systems; targets name their ELF output explicitly.)
