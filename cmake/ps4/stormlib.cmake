# cmake/ps4/stormlib.cmake
#
# Builds the vendored StormLib (extern/StormLib) and its bundled zlib 1.2.5
# for the PlayStation 4 (OpenOrbis toolchain) -- and, for that matter, for any
# host, so the same targets can be smoke-tested natively.
#
# Include it from the top-level CMakeLists.txt *before* any find_package(ZLIB):
#
#     include(cmake/ps4/stormlib.cmake)
#
# It defines:
#
#   wowee_ps4_zlib   STATIC  bundled zlib 1.2.5 (extern/StormLib/src/zlib).
#                            PUBLIC (SYSTEM) include dir so '#include <zlib.h>'
#                            works for the client sources in src/.
#   ZLIB::ZLIB       ALIAS   -> wowee_ps4_zlib (only if no ZLIB::ZLIB exists yet)
#   storm            STATIC  StormLib with the bundled bzip2 / lzma / libtomcrypt /
#                            libtommath / pklib / huffman / adpcm / sparse /
#                            jenkins sources, unicode off, linked to wowee_ps4_zlib
#                            (the zlib inside StormLib is *not* compiled twice).
#   StormLib::storm  ALIAS   -> storm (created by extern/StormLib/CMakeLists.txt)
#
# It also pre-seeds the FindZLIB result variables (ZLIB_FOUND, ZLIB_LIBRARY,
# ZLIB_INCLUDE_DIR, ZLIB_LIBRARIES, ZLIB_INCLUDE_DIRS, ZLIB_VERSION_STRING) so a
# later find_package(ZLIB REQUIRED) succeeds inside the cross sysroot (which has
# no zlib) and resolves to the bundled copy instead of a host library.
#
# extern/StormLib/CMakeLists.txt is used as-is (add_subdirectory); the only
# post-processing is removing the src/zlib/*.c sources from 'storm' and pointing
# it at wowee_ps4_zlib via __SYS_ZLIB, so there is exactly one zlib in the link.
#
# Porting notes (see README.md):
#   * The OpenOrbis target triple is x86_64-pc-freebsd12-elf, so the compiler
#     predefines __FreeBSD__ (not __linux__). StormPort.h therefore takes its
#     generic "Linux-like" branch (STORMLIB_LINUX + STORMLIB_HAS_MMAP).
#   * The libc is musl with a 64-bit off_t. With _GNU_SOURCE musl's own headers
#     provide '#define stat64 stat', 'fstat64', 'lseek64', 'ftruncate64',
#     'off64_t' and 'O_LARGEFILE 0', which is exactly what FileStream.cpp needs.
#     Upstream StormLib only adds those for CMAKE_SYSTEM_NAME=*BSD; our
#     toolchain file says "Generic", so this fragment defines _GNU_SOURCE for the
#     storm target explicitly instead of relying on the toolchain flags alone.

include_guard(GLOBAL)

if(TARGET wowee_ps4_zlib OR TARGET storm)
    message(STATUS "cmake/ps4/stormlib.cmake: targets already defined, skipping")
    return()
endif()

get_filename_component(WOWEE_STORMLIB_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../extern/StormLib" ABSOLUTE)
set(WOWEE_PS4_ZLIB_DIR "${WOWEE_STORMLIB_DIR}/src/zlib")

if(NOT EXISTS "${WOWEE_STORMLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "StormLib sources not found at ${WOWEE_STORMLIB_DIR}")
endif()

# StormLib's own CMakeLists declares LANGUAGES C CXX; make sure both are on.
get_property(_wowee_langs GLOBAL PROPERTY ENABLED_LANGUAGES)
if(NOT "C" IN_LIST _wowee_langs)
    enable_language(C)
endif()
if(NOT "CXX" IN_LIST _wowee_langs)
    enable_language(CXX)
endif()
unset(_wowee_langs)

# ---------------------------------------------------------------------------
# (a) bundled zlib 1.2.5 -> wowee_ps4_zlib + ZLIB::ZLIB
# ---------------------------------------------------------------------------
# Same file list as ZLIB_FILES in extern/StormLib/CMakeLists.txt, plus
# ps4/zlib/uncompr.c: the vendored zlib has no uncompr.c (StormLib never calls
# uncompress()) but the client does, so the missing 1.2.5 file lives next to
# this fragment rather than in the vendored tree. The compress_zlib.c wrapper
# (an MSVC name-clash workaround that just #includes compress.c) and the gz*
# file API are deliberately not built.
get_filename_component(WOWEE_PS4_ZLIB_EXTRA_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../ps4/zlib" ABSOLUTE)
set(WOWEE_PS4_ZLIB_SOURCES
    "${WOWEE_PS4_ZLIB_EXTRA_DIR}/uncompr.c"
    "${WOWEE_PS4_ZLIB_DIR}/adler32.c"
    "${WOWEE_PS4_ZLIB_DIR}/compress.c"
    "${WOWEE_PS4_ZLIB_DIR}/crc32.c"
    "${WOWEE_PS4_ZLIB_DIR}/deflate.c"
    "${WOWEE_PS4_ZLIB_DIR}/inffast.c"
    "${WOWEE_PS4_ZLIB_DIR}/inflate.c"
    "${WOWEE_PS4_ZLIB_DIR}/inftrees.c"
    "${WOWEE_PS4_ZLIB_DIR}/trees.c"
    "${WOWEE_PS4_ZLIB_DIR}/zutil.c"
)
foreach(_src IN LISTS WOWEE_PS4_ZLIB_SOURCES)
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR "bundled zlib source missing: ${_src}")
    endif()
endforeach()

add_library(wowee_ps4_zlib STATIC ${WOWEE_PS4_ZLIB_SOURCES})
target_include_directories(wowee_ps4_zlib SYSTEM PUBLIC
    $<BUILD_INTERFACE:${WOWEE_PS4_ZLIB_DIR}>)
# zlib 1.2.5 is plain C89; keep warnings from its old-style code out of -Werror
# builds of the client without touching the vendored sources.
target_compile_options(wowee_ps4_zlib PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-w>)
set_target_properties(wowee_ps4_zlib PROPERTIES
    OUTPUT_NAME "wowee_ps4_zlib"
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99)

if(TARGET ZLIB::ZLIB)
    message(WARNING
        "cmake/ps4/stormlib.cmake: a ZLIB::ZLIB target already exists (probably "
        "from a host find_package(ZLIB) that ran before this fragment). The PS4 "
        "client must link wowee_ps4_zlib, not the host zlib -- include this "
        "fragment before find_package(ZLIB).")
else()
    add_library(ZLIB::ZLIB ALIAS wowee_ps4_zlib)
endif()

# Pre-seed FindZLIB so 'find_package(ZLIB REQUIRED)' later in the tree passes
# (its REQUIRED_VARS are ZLIB_LIBRARY and ZLIB_INCLUDE_DIR; with ZLIB::ZLIB
# already a target it does not create an IMPORTED one). ZLIB_LIBRARY holding a
# target name is fine: target names are valid in target_link_libraries().
set(ZLIB_INCLUDE_DIR "${WOWEE_PS4_ZLIB_DIR}" CACHE PATH
    "zlib include dir (bundled StormLib zlib 1.2.5)" FORCE)
set(ZLIB_LIBRARY "wowee_ps4_zlib" CACHE STRING
    "zlib library (bundled StormLib zlib 1.2.5 target)" FORCE)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR}")
set(ZLIB_LIBRARIES "${ZLIB_LIBRARY}")
set(ZLIB_VERSION_STRING "1.2.5")
set(ZLIB_VERSION "1.2.5")

# ---------------------------------------------------------------------------
# (b) StormLib -> storm (+ StormLib::storm alias from upstream)
# ---------------------------------------------------------------------------
# Upstream options: everything bundled, no install/CPack, no tests, ANSI (not
# UNICODE -- that option is Windows-only upstream anyway). BUILD_SHARED_LIBS is
# forced off by the PS4 toolchain file; force it here too for native builds.
set(STORM_USE_BUNDLED_LIBRARIES ON  CACHE BOOL "StormLib: use bundled zlib/bzip2/tomcrypt/tommath" FORCE)
set(STORM_SKIP_INSTALL          ON  CACHE BOOL "StormLib: no install rules" FORCE)
set(STORM_BUILD_TESTS           OFF CACHE BOOL "StormLib: no test app" FORCE)
set(STORM_UNICODE               OFF CACHE BOOL "StormLib: ANSI build" FORCE)
set(WITH_BUNDLED_LIBTOMMATH     ON  CACHE BOOL "StormLib: bundled libtommath" FORCE)
set(WITH_BUNDLED_LIBTOMCRYPT    ON  CACHE BOOL "StormLib: bundled libtomcrypt" FORCE)
set(_wowee_saved_bsl "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)

add_subdirectory("${WOWEE_STORMLIB_DIR}" "${CMAKE_BINARY_DIR}/extern/StormLib-wowee")

set(BUILD_SHARED_LIBS "${_wowee_saved_bsl}")
unset(_wowee_saved_bsl)

if(NOT TARGET storm)
    message(FATAL_ERROR "extern/StormLib did not define the 'storm' target")
endif()

# Drop the zlib objects StormLib would otherwise compile into libstorm.a and
# make StormCommon.h use <zlib.h> (__SYS_ZLIB) -- which resolves to the very
# same bundled header through wowee_ps4_zlib's PUBLIC include dir. Result: one
# zlib in the final link, shared by StormLib and the client.
get_target_property(_storm_sources storm SOURCES)
set(_storm_kept "")
set(_storm_dropped 0)
foreach(_src IN LISTS _storm_sources)
    if(_src MATCHES "(^|/)src/zlib/[^/]+\\.c$")
        math(EXPR _storm_dropped "${_storm_dropped} + 1")
    else()
        list(APPEND _storm_kept "${_src}")
    endif()
endforeach()
if(NOT _storm_dropped EQUAL 9)
    message(FATAL_ERROR
        "cmake/ps4/stormlib.cmake: expected to remove 9 bundled zlib sources "
        "from 'storm', removed ${_storm_dropped}. extern/StormLib/CMakeLists.txt "
        "changed shape; update this fragment.")
endif()
set_property(TARGET storm PROPERTY SOURCES ${_storm_kept})
unset(_storm_sources)
unset(_storm_kept)
unset(_storm_dropped)

target_compile_definitions(storm PRIVATE __SYS_ZLIB)
target_link_libraries(storm PUBLIC wowee_ps4_zlib)

# StormLib's FileStream.cpp calls the LFS64 names (stat64, lseek64, off64_t,
# O_LARGEFILE). On the OpenOrbis musl those are provided as macros over the
# 64-bit-off_t base functions when _GNU_SOURCE is defined (fcntl.h, sys/stat.h,
# unistd.h). The toolchain file already passes -D_GNU_SOURCE; repeat it here so
# 'storm' does not depend on that detail. Harmless on glibc/host builds.
target_compile_definitions(storm PRIVATE _GNU_SOURCE)
# ps4_kstat.h: the kernel's real struct stat layout, used by FileStream.cpp on
# the console (the toolchain's declaration mis-sizes mode_t; see the header).
get_filename_component(WOWEE_PS4_COMPAT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ps4/compat" ABSOLUTE)
target_include_directories(storm PRIVATE "${WOWEE_PS4_COMPAT_DIR}")
set_target_properties(storm PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The vendored third-party code (bzip2, lzma SDK, libtom*) is noisy under
# clang 18; silence it without touching the sources.
target_compile_options(storm PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang,AppleClang,GNU>,$<CXX_COMPILER_ID:Clang,AppleClang,GNU>>:-w>)

if(WOWEE_PS4)
    message(STATUS "StormLib for PS4: storm + wowee_ps4_zlib (bundled zlib 1.2.5, bzip2, lzma, libtomcrypt, libtommath)")
endif()

set(WOWEE_PS4_STORMLIB_TARGETS storm wowee_ps4_zlib)
