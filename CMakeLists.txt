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
#   cmake --install build --prefix <dir>   # installs <dir>/GMIC.ofx.bundle
#
# Options: -DOFX_CXX_STANDARD=20 (default: CMAKE_CXX_STANDARD, or 17; the Makefile's CXXSTD),
# -DOFX_USE_OPENMP=OFF (default ON, the Makefile's OPENMP=1), -DLIBOMP_PREFIX=<prefix> (Apple clang:
# libomp, default /opt/local), -DOFX_DEPLOY_BUNDLES=ON (macOS: the install step copies the MacPorts
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

# C++ standard (OFX_CXX_STANDARD, see cmake/OpenFXLocal.cmake): C++17 by default (Natron RB-2.6
# Qt5 builds), C++20 for Qt6 builds (RB-2.7). Strict -std=c++NN, as the Makefile's CXXSTD and the
# CI builds.
if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Optimization and debug flags of openfx/Examples/Makefile.master (CONFIG=release/debug/
# relwithdebinfo): -O2, not CMake's -O3 (nor -Ofast, whose -ffast-math breaks G'MIC's NaN tests)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wextra") # and DEBUG (add_ofx_plugin)
endif()

# ========================== openfx ==========================

# The OFX C API headers (openfx::Api): the plug-in uses the raw C API, not the Support library.
# EXCLUDE_FROM_ALL: openfx's libraries are not built.
add_subdirectory(openfx EXCLUDE_FROM_ALL)

# The bundle is installed into CMAKE_INSTALL_PREFIX, as openfx-misc and openfx-io do (cmake --install
# build --prefix /usr/OFX/Plugins), unless PLUGIN_INSTALLDIR is given (openfx's default is the
# system's OFX folder)
if(NOT PLUGIN_INSTALLDIR)
  set(PLUGIN_INSTALLDIR ".")
endif()
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(OpenFXLocal) # stop-gap for openfx's add_ofx_plugin etc.: see the file

# ========================== G'MIC sources ==========================
#
# Downloaded and verified as the top-level Makefile's "sources" target does, and extracted into the
# build tree (never into the source directory). The version, checksums, mirrors and file list are
# read from the Makefile, which stays the single place to change when upgrading G'MIC
# (GMICVER, GMICSRC_SHA256_<version>). -DGMICVER=<version> -DGMICSRC_SHA256=<sha256> override them,
# as on the make command line.

set(GMIC_MAKEFILE "${PROJECT_SOURCE_DIR}/Makefile")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GMIC_MAKEFILE}")
file(STRINGS "${GMIC_MAKEFILE}" _gmic_mk REGEX "^[A-Za-z0-9_.]+ *=")
function(gmic_makefile_var _name _out)
  set(_value "")
  foreach(_line IN LISTS _gmic_mk)
    if(_line MATCHES "^([A-Za-z0-9_.]+) *= *(.*)$" AND CMAKE_MATCH_1 STREQUAL _name)
      string(STRIP "${CMAKE_MATCH_2}" _value)
    endif()
  endforeach()
  set(${_out} "${_value}" PARENT_SCOPE)
endfunction()

if(NOT GMICVER)
  gmic_makefile_var(GMICVER GMICVER)
endif()
if(NOT GMICVER)
  message(FATAL_ERROR "Could not read GMICVER from ${GMIC_MAKEFILE}")
endif()
if(NOT GMICSRC_SHA256)
  gmic_makefile_var("GMICSRC_SHA256_${GMICVER}" GMICSRC_SHA256)
endif()
if(NOT GMICSRC_SHA256 MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "No sha256 known for G'MIC ${GMICVER}: add GMICSRC_SHA256_${GMICVER} to the Makefile "
    "(or configure with -DGMICSRC_SHA256=<sha256>)")
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
    foreach(_try 1 2 3)
      message(STATUS "Fetching ${_url}")
      file(DOWNLOAD "${_url}" "${_tmp}" STATUS _status TLS_VERIFY ON INACTIVITY_TIMEOUT 60)
      list(GET _status 0 _code)
      if(_code EQUAL 0)
        break()
      endif()
      list(GET _status 1 _msg)
      message(WARNING "Could not download ${_url}: ${_msg}")
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
    message(FATAL_ERROR "Could not download a valid ${GMICSRC}: download it from one of\n  ${GMICSRC_URLS}\n"
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

# ---- Patches to the extracted G'MIC sources go here: they run on the fresh copies in
# ${_gmic_extracted} (add the patch scripts to CMAKE_CONFIGURE_DEPENDS, so that a change re-runs
# them). ----

foreach(_f IN LISTS GMICSOURCES)
  configure_file("${_gmic_extracted}/${_f}" "${GMIC_SRC_DIR}/${_f}" COPYONLY)
endforeach()
file(REMOVE_RECURSE "${GMIC_EXTRACT_DIR}")

# The sources in the source directory include their headers with #include "...", which looks first
# in the source directory: a G'MIC header left there by a Makefile build would be used instead of
# the one in GMIC_SRC_DIR. That is harmless only if it is the same file.
foreach(_f IN LISTS GMICSOURCES)
  if(_f MATCHES "\\.h$" AND EXISTS "${PROJECT_SOURCE_DIR}/${_f}")
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

# Generated headers (none yet) also go into the build tree, e.g. ${CMAKE_BINARY_DIR}/generated,
# added to the include directories below.

set(GMIC_EXTRACTED_SOURCES "")
foreach(_f IN LISTS GMICSOURCES)
  list(APPEND GMIC_EXTRACTED_SOURCES "${GMIC_SRC_DIR}/${_f}")
endforeach()

# ========================== the plug-in ==========================

# the .cpp files of GMICSOURCES (gmic.cpp, gmic_libc.cpp) and those of GMIC_OFX/Makefile
add_ofx_plugin(GMIC
  INFO_PLIST GMIC_OFX/Info.plist
  SOURCES
    ${GMIC_EXTRACTED_SOURCES}
    gmic_plugin.cpp
    Helpers/gmic_parser.cpp
    Helpers/RFX_FileUtils.cpp
    Helpers/RFX_StringUtils.cpp)
ofx_plugin_resources(GMIC GMIC_OFX/GMIC.png)
source_group("G'MIC" FILES ${GMIC_EXTRACTED_SOURCES})

target_include_directories(GMIC PRIVATE
  "${GMIC_SRC_DIR}"
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
find_library(FFTW3_THREADS_LIBRARY fftw3_threads HINTS ${_fftw3_libdir} ${GMIC_DEPS_LIBRARY_DIRS} REQUIRED)
target_link_libraries(GMIC PRIVATE PkgConfig::GMIC_DEPS ${FFTW3_THREADS_LIBRARY})

# OpenMP (OFX_USE_OPENMP), except with MSVC, whose OpenMP 2.0 has issues with CImg
# (https://github.com/dtschump/CImg/issues/208)
if(OFX_USE_OPENMP AND NOT MSVC)
  ofx_target_openmp(GMIC)
  target_compile_definitions(GMIC PRIVATE cimg_use_openmp)
endif()

ofx_deploy_bundle(GMIC)

message(STATUS "G'MIC ${GMICVER} (${_gmic_tarball}), C++${OFX_CXX_STANDARD}, OpenMP: ${OFX_USE_OPENMP}, "
  "bundle: ${OFX_PLUGIN_BUILD_DIR}/GMIC.ofx.bundle/Contents/${ARCHDIR}")
