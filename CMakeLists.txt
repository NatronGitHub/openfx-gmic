# ***** BEGIN LICENSE BLOCK *****
# This file is part of openfx-gmic <https://github.com/NatronGitHub/openfx-gmic>,
# Copyright (C) 2013-2018 INRIA
#
# openfx-gmic is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# openfx-gmic is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with openfx-gmic.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****

# CMake build of GMIC.ofx.bundle, equivalent to the Makefiles (Makefile, GMIC_OFX/Makefile and
# openfx/Examples/Makefile.master):
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # Debug, RelWithDebInfo
#   cmake --build build                    # assembles build/Plugins/GMIC.ofx.bundle
#   cmake --install build --prefix <dir>   # installs <dir>/OFX/Plugins/GMIC.ofx.bundle
#
# Options: -DOFX_CXX_STANDARD=20 (default: CMAKE_CXX_STANDARD, or 17; the Makefile's CXXSTD),
# -DOFX_USE_OPENMP=OFF (default ON, the Makefile's OPENMP=1), -DOFX_LIBOMP_ROOT=<dir> (Apple clang:
# libomp, default MacPorts'), -DOFX_DEPLOY_BUNDLES=ON (macOS: the install step copies the MacPorts
# libraries into the bundle, as the Makefile's release builds do), -DGMIC_TARBALL=<path> (a
# downloaded gmic_<version>.tar.gz, for offline builds), -DGMICVER=... -DGMICSRC_SHA256=...

cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Prevented in-tree build: use a build directory (cmake -B build).")
endif()

project(OpenFXGMIC CXX)

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/openfx/include")
  message(FATAL_ERROR
    "\n submodule(s) are missing, please update your repository:\n"
    "  > git submodule update -i\n")
endif()

# ========================== build configuration ==========================

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug, Release or RelWithDebInfo" FORCE)
endif()

# C++ standard: OFX_CXX_STANDARD (openfx/cmake/OpenFX.cmake; default: CMAKE_CXX_STANDARD, or 17):
# C++17 for Natron RB-2.6 (Qt5) builds, C++20 for Qt6 builds (RB-2.7). add_ofx_plugin compiles
# with strict -std=c++NN (no GNU extensions), as the Makefile's CXXSTD and the CI builds do.

# ========================== openfx ==========================

# openfx's cmake/OpenFX.cmake: add_ofx_plugin (which links the OFX C API, openfx::Api: the plug-in
# doesn't use the Support library), ofx_plugin_resources, ofx_target_openmp, ofx_deploy_bundle, and
# the OFX_CXX_STANDARD, OFX_USE_OPENMP, OFX_DEPLOY_BUNDLES and PLUGIN_INSTALLDIR options. The
# bundle is installed into ${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}, by default
# <prefix>/OFX/Plugins (e.g. /usr/OFX/Plugins with the prefix /usr).
# EXCLUDE_FROM_ALL: openfx's libraries are not built.
add_subdirectory(openfx EXCLUDE_FROM_ALL)

# ========================== G'MIC sources ==========================
#
# Downloaded and verified as the top-level Makefile's "sources" target does, and extracted into the
# build tree (never into the source directory). The version, checksums, mirrors and file list are
# read from the Makefile, which stays the single place to change when upgrading G'MIC
# (GMICVER, GMICSRC_SHA256_<version>). -DGMICVER=<version> -DGMICSRC_SHA256=<sha256> override them,
# as on the make command line; since CMake keeps them in its cache, a warning tells when they differ
# from the Makefile (e.g. after a GMICVER bump): remove them with -UGMICVER -UGMICSRC_SHA256.

set(GMIC_MAKEFILE "${PROJECT_SOURCE_DIR}/Makefile")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GMIC_MAKEFILE}")
file(STRINGS "${GMIC_MAKEFILE}" _gmic_mk REGEX "^[A-Za-z0-9_.]+ *=")
# Only simple assignments are understood (one line, "NAME = value", not inside ifeq): fail on
# anything else, rather than read a wrong value.
function(gmic_makefile_var _name _out)
  set(_value "")
  set(_count 0)
  foreach(_line IN LISTS _gmic_mk)
    if(_line MATCHES "^([A-Za-z0-9_.]+) *= *(.*)$" AND CMAKE_MATCH_1 STREQUAL _name)
      string(STRIP "${CMAKE_MATCH_2}" _value)
      math(EXPR _count "${_count} + 1")
    endif()
  endforeach()
  if(_count GREATER 1 OR _value MATCHES "\\\\$")
    message(FATAL_ERROR "${GMIC_MAKEFILE}: ${_name} must be assigned once, on one line (it is parsed by CMakeLists.txt)")
  endif()
  set(${_out} "${_value}" PARENT_SCOPE)
endfunction()

gmic_makefile_var(GMICVER _mk_gmicver)
if(NOT _mk_gmicver)
  message(FATAL_ERROR "Could not read GMICVER from ${GMIC_MAKEFILE}")
endif()
if(NOT GMICVER)
  set(GMICVER "${_mk_gmicver}")
elseif(NOT GMICVER STREQUAL _mk_gmicver)
  message(WARNING "GMICVER=${GMICVER} (command line or cache) overrides the Makefile's ${_mk_gmicver}: "
    "remove it with -UGMICVER")
endif()
gmic_makefile_var("GMICSRC_SHA256_${GMICVER}" _mk_sha256)
if(NOT GMICSRC_SHA256)
  set(GMICSRC_SHA256 "${_mk_sha256}")
elseif(_mk_sha256 AND NOT GMICSRC_SHA256 STREQUAL _mk_sha256)
  message(WARNING "GMICSRC_SHA256=${GMICSRC_SHA256} (command line or cache) overrides the Makefile's "
    "GMICSRC_SHA256_${GMICVER}: remove it with -UGMICSRC_SHA256")
endif()
string(LENGTH "${GMICSRC_SHA256}" _len)
if(NOT _len EQUAL 64 OR NOT GMICSRC_SHA256 MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "No valid sha256 (64 hex digits) for G'MIC ${GMICVER}: add GMICSRC_SHA256_${GMICVER} "
    "to the Makefile (or configure with -DGMICSRC_SHA256=<sha256>)")
endif()
string(TOLOWER "${GMICSRC_SHA256}" GMICSRC_SHA256)
foreach(_var GMICSRC GMICSRCDIR GMICSRC_URLS GMICSOURCES)
  gmic_makefile_var(${_var} ${_var})
  string(REPLACE "$(GMICVER)" "${GMICVER}" ${_var} "${${_var}}")
  string(REPLACE "$(GMICSRC)" "${GMICSRC}" ${_var} "${${_var}}")
  if(NOT ${_var} OR ${_var} MATCHES "\\$")
    message(FATAL_ERROR "Could not read ${_var} from ${GMIC_MAKEFILE}")
  endif()
  separate_arguments(${_var} UNIX_COMMAND "${${_var}}")
endforeach()

# The tarball: GMIC_TARBALL if given (downloaded there if missing), else a gmic_<version>.tar.gz
# left in the source directory by the Makefile, else downloaded into GMIC_DOWNLOAD_DIR.
set(GMIC_TARBALL "" CACHE FILEPATH "G'MIC source tarball (gmic_<version>.tar.gz), downloaded there if missing")
set(GMIC_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Where to download the G'MIC source tarball")
if(GMIC_TARBALL)
  set(_gmic_tarball "${GMIC_TARBALL}")
  get_filename_component(_name "${GMIC_TARBALL}" NAME)
  if(NOT _name STREQUAL GMICSRC)
    message(WARNING "GMIC_TARBALL=${GMIC_TARBALL} (command line or cache) is not ${GMICSRC}: "
      "its checksum is verified anyway; remove it with -UGMIC_TARBALL")
  endif()
elseif(EXISTS "${PROJECT_SOURCE_DIR}/${GMICSRC}")
  set(_gmic_tarball "${PROJECT_SOURCE_DIR}/${GMICSRC}")
else()
  set(_gmic_tarball "${GMIC_DOWNLOAD_DIR}/${GMICSRC}")
endif()

if(EXISTS "${_gmic_tarball}")
  file(SHA256 "${_gmic_tarball}" _sum)
  if(NOT _sum STREQUAL GMICSRC_SHA256)
    message(FATAL_ERROR "${_gmic_tarball}: checksum mismatch (got ${_sum}, expected ${GMICSRC_SHA256}): remove it")
  endif()
else()
  # Each mirror in turn, as the Makefile (gmic.eu may answer 403). The checksum is verified here,
  # not with file(DOWNLOAD EXPECTED_HASH), which stops at the first mismatch instead of trying
  # the next mirror; the file gets its final name only once verified.
  set(_tmp "${_gmic_tarball}.tmp")
  set(_ok FALSE)
  foreach(_url IN LISTS GMICSRC_URLS)
    # up to 3 tries, 5 s apart, except after an HTTP 4xx answer (e.g. gmic.eu's 403)
    foreach(_try 1 2 3)
      if(_try GREATER 1)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 5)
      endif()
      message(STATUS "Fetching ${_url}")
      file(DOWNLOAD "${_url}" "${_tmp}" STATUS _status LOG _log TLS_VERIFY ON INACTIVITY_TIMEOUT 60)
      list(GET _status 0 _code)
      if(_code EQUAL 0)
        break()
      endif()
      list(GET _status 1 _msg)
      message(WARNING "Could not download ${_url}: ${_msg}")
      if(_log MATCHES "HTTP/[0-9.]+ 4[0-9][0-9]")
        break()
      endif()
    endforeach()
    if(_code EQUAL 0)
      file(SHA256 "${_tmp}" _sum)
      if(_sum STREQUAL GMICSRC_SHA256)
        file(RENAME "${_tmp}" "${_gmic_tarball}")
        set(_ok TRUE)
        break()
      endif()
      message(WARNING "Checksum mismatch for ${_url} (got ${_sum})")
    endif()
  endforeach()
  file(REMOVE "${_tmp}")
  if(NOT _ok)
    string(REPLACE ";" "\n  " _urls "${GMICSRC_URLS}")
    message(FATAL_ERROR "Could not download a valid ${GMICSRC}: download it from one of\n  ${_urls}\n"
      "and configure with -DGMIC_TARBALL=<path>")
  endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gmic_tarball}")

# Extract GMICSOURCES afresh at each configure (it takes a fraction of a second) into
# GMIC_EXTRACT_DIR, where they may be patched, then copy them into GMIC_SRC_DIR, which is
# what the build uses. The copy only writes files that changed, so that a new configure doesn't
# recompile anything.
set(GMIC_EXTRACT_DIR "${CMAKE_BINARY_DIR}/gmic-extract")
set(GMIC_SRC_DIR "${CMAKE_BINARY_DIR}/gmic")
file(REMOVE_RECURSE "${GMIC_EXTRACT_DIR}")
set(_patterns "")
foreach(_f IN LISTS GMICSOURCES)
  list(APPEND _patterns "${GMICSRCDIR}/src/${_f}")
endforeach()
file(MAKE_DIRECTORY "${GMIC_EXTRACT_DIR}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_gmic_tarball}" ${_patterns}
  WORKING_DIRECTORY "${GMIC_EXTRACT_DIR}" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
  message(FATAL_ERROR "Could not extract the G'MIC sources from ${_gmic_tarball}")
endif()
set(_gmic_extracted "${GMIC_EXTRACT_DIR}/${GMICSRCDIR}/src")
foreach(_f IN LISTS GMICSOURCES)
  if(NOT EXISTS "${_gmic_extracted}/${_f}")
    message(FATAL_ERROR "${GMICSRC} has no ${GMICSRCDIR}/src/${_f}")
  endif()
endforeach()

# Local patches to the extracted G'MIC sources, the same as the Makefile's (see patches/): each
# <script>:<file>:<where> runs "sh patches/<script> <file>" on the fresh copy, and fails if the
# source doesn't contain the expected code (e.g. after a GMICVER bump). <where> is the platforms
# whose compiled code the patch changes: ALL, or a CMake condition variable such as APPLE.
# Adding a patch is one line here, and two in the Makefile (the case of the extraction rule, and
# the dependency of the patched file on the script).
# The patches need a POSIX sh (Linux, macOS, MSYS2; with MSVC, e.g. Git for Windows' sh.exe in the
# PATH, or -DGMIC_PATCH_SH=<path>). Without one, only the patches that don't change this platform's
# code are skipped (with a warning); a patch for ALL platforms (e.g. a CImg.h fix) makes sh
# required everywhere.
set(GMIC_PATCHES
  gmic-apple-abort.sh:gmic.h:APPLE   # abort support on macOS (#41)
  gmic-apple-abort.sh:gmic.cpp:APPLE
)
find_program(GMIC_PATCH_SH sh)
foreach(_patch IN LISTS GMIC_PATCHES)
  string(REPLACE ":" ";" _patch "${_patch}")
  list(GET _patch 0 _script)
  list(GET _patch 1 _file)
  list(GET _patch 2 _where)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/patches/${_script}")
  if(NOT GMIC_PATCH_SH)
    if(_where STREQUAL "ALL" OR ${_where})
      message(FATAL_ERROR "sh not found: it is needed to apply patches/${_script} to ${_file}, which "
        "changes the code compiled on this platform: put a POSIX sh in the PATH, or configure with "
        "-DGMIC_PATCH_SH=<path to sh>")
    endif()
    message(WARNING "sh not found: patches/${_script} is not applied to ${_file} (it only changes "
      "${_where} code)")
    continue()
  endif()
  execute_process(COMMAND "${GMIC_PATCH_SH}" "${PROJECT_SOURCE_DIR}/patches/${_script}" "${_file}"
    WORKING_DIRECTORY "${_gmic_extracted}" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "patches/${_script} failed on ${_file} of G'MIC ${GMICVER}")
  endif()
endforeach()

foreach(_f IN LISTS GMICSOURCES)
  configure_file("${_gmic_extracted}/${_f}" "${GMIC_SRC_DIR}/${_f}" COPYONLY)
endforeach()
file(REMOVE_RECURSE "${GMIC_EXTRACT_DIR}")

# The sources in the source directory include their headers with #include "...", which looks first
# in the source directory: a G'MIC header left there by a Makefile build would be used instead of
# the one in GMIC_SRC_DIR. That is harmless only if it is the same file. This is checked at configure
# time, again when such a header changes, but not when one appears after the configure (run cmake
# again after a Makefile build in the same checkout).
foreach(_f IN LISTS GMICSOURCES)
  if(_f MATCHES "\\.h$" AND EXISTS "${PROJECT_SOURCE_DIR}/${_f}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${_f}")
    file(SHA256 "${PROJECT_SOURCE_DIR}/${_f}" _stray)
    file(SHA256 "${GMIC_SRC_DIR}/${_f}" _ours)
    if(NOT _stray STREQUAL _ours)
      message(FATAL_ERROR "${PROJECT_SOURCE_DIR}/${_f} is not the ${_f} of G'MIC ${GMICVER} "
        "(left by a Makefile build of another version?), and would be included instead of "
        "${GMIC_SRC_DIR}/${_f}: remove the G'MIC sources from the source directory (make distclean), "
        "or update them (make sources)")
    endif()
  endif()
endforeach()

# Hot fixes for G'MIC bugs (see gmic_hotfixes.gmic and the top-level Makefile), embedded as a
# null-terminated byte array into gmic_hotfixes.h, generated into the build tree (GMIC_GENERATED_DIR,
# an include directory; gmic_plugin.cpp includes it with <...>, so that a header left in the
# source directory by a Makefile build is not used). They are written for one G'MIC version: fail
# if gmic.h is another one, so that each fix gets reviewed.
set(GMIC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(GMIC_HOTFIXES "${PROJECT_SOURCE_DIR}/gmic_hotfixes.gmic")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GMIC_HOTFIXES}")
file(STRINGS "${GMIC_SRC_DIR}/gmic.h" _line REGEX "^#define gmic_version [0-9]+")
string(REGEX REPLACE "^#define gmic_version ([0-9]+).*" "\\1" _gmic_version_number "${_line}")
file(STRINGS "${GMIC_HOTFIXES}" _line REGEX "^#@openfx_gmic_hotfixes ")
string(REGEX REPLACE "^#@openfx_gmic_hotfixes ([0-9.]+).*" "\\1" _hotfixes_version "${_line}")
string(REPLACE "." "" _hotfixes_version_number "${_hotfixes_version}")
if(NOT _hotfixes_version_number STREQUAL _gmic_version_number)
  message(FATAL_ERROR "gmic_hotfixes.gmic is for G'MIC ${_hotfixes_version}, gmic.h is version "
    "${_gmic_version_number}: review each fix, see the top of gmic_hotfixes.gmic")
endif()
file(READ "${GMIC_HOTFIXES}" _hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(REPLACE "0x0d," "" _bytes "${_bytes}") # carriage returns (Windows checkouts)
file(WRITE "${GMIC_GENERATED_DIR}/gmic_hotfixes.h.tmp"
  "// Generated from gmic_hotfixes.gmic by CMake: do not edit.\nstatic const unsigned char gmic_hotfixes[] = {\n${_bytes}\n0x00 };\n")
# only rewritten when it changes, so that a new configure does not recompile gmic_plugin.cpp
configure_file("${GMIC_GENERATED_DIR}/gmic_hotfixes.h.tmp" "${GMIC_GENERATED_DIR}/gmic_hotfixes.h" COPYONLY)
file(REMOVE "${GMIC_GENERATED_DIR}/gmic_hotfixes.h.tmp")

set(GMIC_EXTRACTED_SOURCES "")
foreach(_f IN LISTS GMICSOURCES)
  list(APPEND GMIC_EXTRACTED_SOURCES "${GMIC_SRC_DIR}/${_f}")
endforeach()

# ========================== the plug-in ==========================

# the .cpp files of GMICSOURCES (gmic.cpp) and those of GMIC_OFX/Makefile
# (gmic_plugin_call.cpp replaces gmic_libc.cpp)
add_ofx_plugin(GMIC
  INFO_PLIST GMIC_OFX/Info.plist
  SOURCES
    ${GMIC_EXTRACTED_SOURCES}
    "${GMIC_GENERATED_DIR}/gmic_hotfixes.h"
    gmic_plugin.cpp
    gmic_plugin_call.cpp
    gmic_plugin_call.h
    Helpers/gmic_parser.cpp
    Helpers/RFX_FileUtils.cpp
    Helpers/RFX_StringUtils.cpp)
ofx_plugin_resources(GMIC GMIC_OFX/GMIC.png) # before ofx_deploy_bundle (below)
source_group("G'MIC" FILES ${GMIC_EXTRACTED_SOURCES})

# Optimization and warnings of openfx/Examples/Makefile.master (CONFIG=release/debug): -O2 instead
# of CMake's default -O3 (an -O given in CMAKE_CXX_FLAGS_RELEASE is kept), -Wall -Wextra in debug
# builds. Not -Ofast: its -ffast-math breaks G'MIC's NaN tests.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NOT CMAKE_CXX_FLAGS_RELEASE MATCHES "-O")
    target_compile_options(GMIC PRIVATE $<$<CONFIG:Release>:-O2>)
  elseif(CMAKE_CXX_FLAGS_RELEASE STREQUAL "-O3 -DNDEBUG") # CMake's default
    target_compile_options(GMIC PRIVATE $<$<CONFIG:Release>:-O2>)
  endif()
  target_compile_options(GMIC PRIVATE $<$<CONFIG:Debug>:-Wall -Wextra>)
endif()

target_include_directories(GMIC PRIVATE
  "${GMIC_SRC_DIR}"
  "${GMIC_GENERATED_DIR}"
  Helpers
  openfx/Examples/include) # ofxUtilities.H

# GMIC_OFX/Makefile's CXXFLAGS. G'MIC >= 3 compiles CImg in its own namespace (gmic_library), so it
# cannot clash with the cimg_library of other plug-ins (e.g. openfx-misc) loaded in the same host.
target_compile_definitions(GMIC PRIVATE
  OFX_EXTENSIONS_NATRON
  OFX_EXTENSIONS_NUKE
  OFX_PLUGIN
  gmic_core
  gmic_is_parallel
  cimg_use_abort
  cimg_display=0
  "cimg_appname=\"gmic\""
  cimg_verbosity=0
  cimg_use_fftw3 # FFTW: Fourier transforms of images with dimensions that are not powers of two
  cimg_use_zlib  # zlib: decompress the G'MIC standard library
  cimg_use_curl  # libcurl: download filter data
  cimg_use_png)  # libpng: HaldCLUTs
if(MSVC)
  target_compile_definitions(GMIC PRIVATE NOMINMAX)
endif()

# The libraries of GMIC_OFX/Makefile, through pkg-config as it does (so that the same ones are
# found, e.g. MacPorts' zlib and libcurl rather than the macOS SDK's), plus fftw3_threads
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMIC_DEPS REQUIRED IMPORTED_TARGET fftw3 zlib libcurl libpng)
pkg_get_variable(_fftw3_libdir fftw3 libdir)
find_library(FFTW3_THREADS_LIBRARY fftw3_threads HINTS ${_fftw3_libdir} ${GMIC_DEPS_LIBRARY_DIRS})
if(NOT FFTW3_THREADS_LIBRARY)
  message(FATAL_ERROR "fftw3_threads not found (in ${_fftw3_libdir})")
endif()
target_link_libraries(GMIC PRIVATE PkgConfig::GMIC_DEPS ${FFTW3_THREADS_LIBRARY})

# OpenMP (OFX_USE_OPENMP), except with MSVC, whose OpenMP 2.0 has issues with CImg
# (https://github.com/dtschump/CImg/issues/208)
if(OFX_USE_OPENMP AND NOT MSVC)
  ofx_target_openmp(GMIC REQUIRED)
  target_compile_definitions(GMIC PRIVATE cimg_use_openmp)
endif()

ofx_deploy_bundle(GMIC)

message(STATUS "G'MIC ${GMICVER} (${_gmic_tarball}), C++${OFX_CXX_STANDARD}, OpenMP: ${OFX_USE_OPENMP}, "
  "bundle: ${OFX_PLUGIN_BUILD_DIR}/GMIC.ofx.bundle/Contents/${ARCHDIR}")
