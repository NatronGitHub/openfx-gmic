# ***** BEGIN LICENSE BLOCK *****
# This file is part of openfx-gmic <https://github.com/NatronGitHub/openfx-gmic>,
# Copyright (C) 2013-2018 INRIA
#
# This file is 'dual-licensed', you have to choose one of the two licenses below to apply
# (see COPYING), as the rest of openfx-gmic:
#
#     CeCILL-C
#     The CeCILL-C license is close to the GNU LGPL.
#     ( http://www.cecill.info/licences/Licence_CeCILL-C_V1-en.html , Licence_CeCILL-C_V1-en.txt )
#
# or  CeCILL v2.0
#     The CeCILL license is compatible with the GNU GPL.
#     ( http://www.cecill.info/licences/Licence_CeCILL_V2-en.html , Licence_CeCILL_V2-en.txt )
# ***** END LICENSE BLOCK *****

########################################################################
IF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
MESSAGE(FATAL_ERROR "Prevented in-tree built. This is bad practice.")
ENDIF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

########################################################################
# Project setup
########################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(OpenFXGMIC CXX)
ENABLE_TESTING()

include(FindOpenGL REQUIRED)

# C++17 by default (Natron RB-2.6 Qt5 builds); C++20 can be chosen with -DCMAKE_CXX_STANDARD=20
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# Use -Ofast when applicable (implies -ffast-math)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Wextra -Ofast -DNDEBUG")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Ofast -DNDEBUG")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Wextra -Ofast -DNDEBUG")
endif()
SET(OPENFX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/openfx)

# Check that submodule have been initialized and updated
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include)
  message(FATAL_ERROR
    "\n submodule(s) are missing, please update your repository:\n"
    "  > git submodule update -i\n")
endif()

# C++ Include directories
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/SupportExt)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/Helpers)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Examples/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/Plugins/include)

# Define "DEBUG" on debug builds
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# /MP on MSVC
ADD_COMPILE_OPTIONS($<$<CXX_COMPILER_ID:MSVC>:/MP>)

SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

set(OFX_SUPPORT_HEADER_DIR "${OPENFX_PATH}/Support/include")
set(OFX_SUPPORT_LIBRARY_DIR "${OPENFX_PATH}/Support/Library")

FILE(GLOB SUPPORT_SOURCES
    "${OFX_SUPPORT_HEADER_DIR}/*.h"

    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsCore.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsImageEffect.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsInteract.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsLog.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsMultiThread.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsParams.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsProperty.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsPropertyValidation.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/*.h"
)

#ADD_LIBRARY(Support STATIC )
#TARGET_COMPILE_DEFINITIONS(Support PRIVATE OFX_SUPPORTS_OPENGLRENDER OFX_EXTENSIONS_TUTTLE OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_VEGAS NOMINMAX)

# The G'MIC sources are extracted by the top-level Makefile (make sources), which also patches
# them (patches/gmic-apple-abort.sh, issue #41). Apply the patch here too, for sources that were
# extracted otherwise; it does nothing on patched files, and fails if the sources changed.
FOREACH(_gmic_file gmic.h gmic.cpp)
  IF(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_gmic_file})
    MESSAGE(FATAL_ERROR "${_gmic_file} is missing: run 'make sources' first")
  ENDIF()
ENDFOREACH()
FIND_PROGRAM(GMIC_PATCH_SH sh)
IF(GMIC_PATCH_SH)
  FOREACH(_gmic_file gmic.h gmic.cpp)
    EXECUTE_PROCESS(COMMAND ${GMIC_PATCH_SH} patches/gmic-apple-abort.sh ${_gmic_file}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE _gmic_patch_result)
    IF(NOT _gmic_patch_result EQUAL 0)
      MESSAGE(FATAL_ERROR "patches/gmic-apple-abort.sh failed on ${_gmic_file}")
    ENDIF()
  ENDFOREACH()
  SET_PROPERTY(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/patches/gmic-apple-abort.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/gmic.h ${CMAKE_CURRENT_SOURCE_DIR}/gmic.cpp)
ELSEIF(APPLE)
  MESSAGE(FATAL_ERROR "sh not found: cannot apply patches/gmic-apple-abort.sh, which macOS builds need")
ELSE()
  MESSAGE(WARNING "sh not found: patches/gmic-apple-abort.sh not applied (it only changes Apple code)")
ENDIF()

# gmic_plugin_call.cpp replaces gmic_libc.cpp
FILE(GLOB GMIC_SOURCES
  "CImg.h"
  "gmic.cpp"
  "gmic.h"
  "gmic_stdlib_community.h"
  "gmic_libc.h"
  "gmic_plugin.cpp"
  "gmic_plugin_call.cpp"
  "gmic_plugin_call.h"
  "Helpers/gmic_parser.cpp"
  "Helpers/RFX_FileUtils.cpp"
  "Helpers/RFX_StringUtils.cpp"
  "Helpers/*h"
)

# Hot fixes for G'MIC bugs (see gmic_hotfixes.gmic and the top-level Makefile), embedded as a
# null-terminated byte array into gmic_hotfixes.h, in the build directory (searched first, before a
# header left in the sources by a Makefile build). They are written for one G'MIC version: fail if
# gmic.h is another one, so that each fix gets reviewed.
SET(GMIC_HOTFIXES ${CMAKE_CURRENT_SOURCE_DIR}/gmic_hotfixes.gmic)
SET_PROPERTY(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GMIC_HOTFIXES} ${CMAKE_CURRENT_SOURCE_DIR}/gmic.h)
FILE(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/gmic.h GMIC_VERSION_LINE REGEX "^#define gmic_version [0-9]+")
STRING(REGEX REPLACE "^#define gmic_version ([0-9]+).*" "\\1" GMIC_VERSION_NUMBER "${GMIC_VERSION_LINE}")
FILE(STRINGS ${GMIC_HOTFIXES} GMIC_HOTFIXES_VERSION_LINE REGEX "^#@openfx_gmic_hotfixes ")
STRING(REGEX REPLACE "^#@openfx_gmic_hotfixes ([0-9.]+).*" "\\1" GMIC_HOTFIXES_VERSION "${GMIC_HOTFIXES_VERSION_LINE}")
STRING(REPLACE "." "" GMIC_HOTFIXES_VERSION_NUMBER "${GMIC_HOTFIXES_VERSION}")
IF(NOT "${GMIC_HOTFIXES_VERSION_NUMBER}" STREQUAL "${GMIC_VERSION_NUMBER}")
  MESSAGE(FATAL_ERROR "gmic_hotfixes.gmic is for G'MIC ${GMIC_HOTFIXES_VERSION}, gmic.h is version ${GMIC_VERSION_NUMBER}: review each fix, see the top of gmic_hotfixes.gmic")
ENDIF()
FILE(READ ${GMIC_HOTFIXES} GMIC_HOTFIXES_HEX HEX)
STRING(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," GMIC_HOTFIXES_BYTES "${GMIC_HOTFIXES_HEX}")
STRING(REPLACE "0x0d," "" GMIC_HOTFIXES_BYTES "${GMIC_HOTFIXES_BYTES}") # carriage returns
FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gmic_hotfixes.h.tmp
  "// Generated from gmic_hotfixes.gmic by CMake: do not edit.\nstatic const unsigned char gmic_hotfixes[] = {\n${GMIC_HOTFIXES_BYTES}\n0x00 };\n")
# only rewritten when it changes, so that a new configure does not recompile gmic_plugin.cpp
CONFIGURE_FILE(${CMAKE_CURRENT_BINARY_DIR}/gmic_hotfixes.h.tmp ${CMAKE_CURRENT_BINARY_DIR}/gmic_hotfixes.h COPYONLY)
INCLUDE_DIRECTORIES(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

# the licences of the plug-in and G'MIC (COPYING: dual CeCILL-C / CeCILL v2; COMMERCIAL builds use
# CeCILL-C), installed in Contents/Resources with the icon
FILE(GLOB GMIC_RESOURCES
  "GMIC_OFX/GMIC.png"
  "COPYING"
  "Licence_CeCILL-C_V1-en.txt"
  "Licence_CeCILL_V2-en.txt"
)

ADD_LIBRARY(GMIC SHARED ${GMIC_SOURCES} ${SUPPORT_SOURCES})
SET_TARGET_PROPERTIES(GMIC PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(GMIC PROPERTIES SUFFIX ".ofx")

# Add extra flags to the link step of the plugin
if(APPLE)
    set_target_properties(GMIC PROPERTIES LINK_FLAGS "-fvisibility=hidden -exported_symbols_list,${OFX_SUPPORT_HEADER_DIR}/osxSymbols")
    set_target_properties(GMIC PROPERTIES INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
elseif(MINGW)
    set_target_properties(GMIC PROPERTIES LINK_FLAGS "-shared -fvisibility=hidden -Xlinker --version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD" OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
# Linux & FreeBSD
    set_target_properties(GMIC PROPERTIES LINK_FLAGS "-Wl,-fvisibility=hidden,--version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
    set_target_properties(GMIC PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
endif()

TARGET_COMPILE_DEFINITIONS(GMIC PRIVATE OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON NOMINMAX OFX_PLUGIN gmic_core gmic_is_parallel cimg_use_abort cimg_display=0 cimg_appname=\"gmic\" cimg_verbosity=0)
#TARGET_LINK_LIBRARIES(GMIC Support ${OPENGL_gl_LIBRARY})
TARGET_LINK_LIBRARIES(GMIC ${OPENGL_gl_LIBRARY})

# FFTW (GPLv2), as in GMIC_OFX/Makefile: OFX_LICENSE=COMMERCIAL (make LICENSE=COMMERCIAL, Natron's
# NATRON_LICENSE) builds without it, and G'MIC then uses the FFT built into CImg, which only
# handles power-of-two image dimensions.
SET(OFX_LICENSE "GPL" CACHE STRING "GPL, or COMMERCIAL to build without FFTW (GPL)")
SET_PROPERTY(CACHE OFX_LICENSE PROPERTY STRINGS GPL COMMERCIAL)
IF(NOT OFX_LICENSE MATCHES "^(GPL|COMMERCIAL)$")
  MESSAGE(FATAL_ERROR "OFX_LICENSE must be GPL or COMMERCIAL, not \"${OFX_LICENSE}\"")
ENDIF()
IF(OFX_LICENSE STREQUAL "GPL")
  FIND_PACKAGE(PkgConfig REQUIRED)
  PKG_CHECK_MODULES(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
  FIND_LIBRARY(FFTW3_THREADS_LIBRARY fftw3_threads HINTS ${FFTW3_LIBRARY_DIRS})
  IF(NOT FFTW3_THREADS_LIBRARY)
    MESSAGE(FATAL_ERROR "fftw3_threads not found (in ${FFTW3_LIBRARY_DIRS})")
  ENDIF()
  TARGET_COMPILE_DEFINITIONS(GMIC PRIVATE cimg_use_fftw3)
  TARGET_LINK_LIBRARIES(GMIC PkgConfig::FFTW3 ${FFTW3_THREADS_LIBRARY})
ENDIF()

# Find OpenMP
# To compile on homebrew:
# env CXX=/usr/local/opt/llvm/bin/clang++ CC=/usr/local/opt/llvm/bin/clang LDFLAGS=-L/usr/local/opt/llvm/lib cmake ..
find_package(OpenMP)
# MSVC is OpenMP 2.0 and has issues, see https://github.com/dtschump/CImg/issues/208 and https://msdn.microsoft.com/fr-fr/library/ff520162.aspx
if (OPENMP_FOUND AND NOT MSVC)
#if (OPENMP_FOUND)
    #include_directories("${OPENMP_INCLUDES}")
    #link_directories("${OPENMP_LIBRARIES}")
    # set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    # set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${OpenMP_SHARED_LINKER_FLAGS}")
    # set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    SET_TARGET_PROPERTIES(GMIC PROPERTIES COMPILE_FLAGS "${OpenMP_CXX_FLAGS}")
    SET_TARGET_PROPERTIES(GMIC PROPERTIES LINK_FLAGS "${OpenMP_EXE_LINKER_FLAGS}")
    TARGET_LINK_LIBRARIES(GMIC ${OpenMP_CXX_LIB_NAMES})
endif(OPENMP_FOUND AND NOT MSVC)

IF (MSVC)
  # Some files require this option. This breaks compatibility with older linkers.
  SET_TARGET_PROPERTIES(GMIC PROPERTIES COMPILE_FLAGS "/bigobj")
ENDIF ()

# Find and set the arch name.
# http://openeffects.org/documentation/reference/ch02s02.html
SET(OFX_ARCH UNKNOWN)
# Win
if(MINGW)
    set(OFX_ARCH "Win32")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "Win64")
    endif()
# Visual Studio    
elseif(MSVC)
    set(OFX_ARCH "Win32")
    if((${CMAKE_SYSTEM_PROCESSOR} STREQUAL "IA64") OR (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64"))
        set(OFX_ARCH "Win64")
    endif()
# FreeBSD
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(OFX_ARCH "FreeBSD-x86")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "FreeBSD-x86-64")
    endif()
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
        if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
            set(OFX_ARCH FreeBSD-arm-64)
        else()
            set(OFX_ARCH FreeBSD-arm)
        endif()
    endif()
# MacOS
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(OFX_ARCH "MacOS")
# Linux
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(OFX_ARCH "Linux-x86")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        set(OFX_ARCH "Linux-x86-64")
    endif()
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
        if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
            set(OFX_ARCH Linux-arm-64)
        else()
            set(OFX_ARCH Linux-arm)
        endif()
    endif()
endif()

SET(OFX_ARCH_NAME ${OFX_ARCH} CACHE STRING "OpenFX target OS and architecture")
IF("${OFX_ARCH_NAME}" EQUAL "UNKNOWN")
  MESSAGE(SEND_ERROR "OFX_ARCH_NAME was not detected. Install targets will be unavailable. See http://openeffects.org/documentation/reference/ch02s02.html to pick an arch name.")
ELSE()
  INSTALL(TARGETS GMIC
    RUNTIME DESTINATION GMIC.ofx.bundle/Contents/${OFX_ARCH_NAME}
    LIBRARY DESTINATION GMIC.ofx.bundle/Contents/${OFX_ARCH_NAME}
  )

  INSTALL(FILES ${GMIC_RESOURCES}
    DESTINATION GMIC.ofx.bundle/Contents/Resources
  )

  INSTALL(FILES Info.plist
    DESTINATION GMIC.ofx.bundle/Contents
  )

  IF(OFX_LICENSE STREQUAL "GPL" AND ("${OFX_ARCH_NAME}" EQUAL "Win32" OR "${OFX_ARCH_NAME}" EQUAL "Win64"))
    INSTALL(FILES libfftw3-3.dll
      DESTINATION GMIC.ofx.bundle/Contents/${OFX_ARCH_NAME}
    )
  ENDIF()

ENDIF()
