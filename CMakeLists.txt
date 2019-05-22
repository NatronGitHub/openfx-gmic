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

set (CMAKE_CXX_STANDARD 11)

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

FILE(GLOB GMIC_SOURCES
  "CImg.h"
  "gmic.cpp"
  "gmic.h"
  "gmic_stdlib.h"
  "Helpers/RFX_FileUtils.cpp"
  "Helpers/RFX_StringUtils.cpp"
  "Helpers/*h"
)

FILE(GLOB GMIC_RESOURCES
  "GMIC_OFX/GMIC.png"
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

TARGET_COMPILE_DEFINITIONS(GMIC PRIVATE OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON NOMINMAX OFX_PLUGIN gmic_build gmic_is_parallel cimg_use_abort cimg_display=0 cimg_appname=\"gmic\" cimg_namespace_suffix=openfx_gmic cimg_verbosity=0 cimg_library=cimg_library_openfx_gmic)
#TARGET_LINK_LIBRARIES(GMIC Support ${OPENGL_gl_LIBRARY})
TARGET_LINK_LIBRARIES(GMIC ${OPENGL_gl_LIBRARY})

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

  IF("${OFX_ARCH_NAME}" EQUAL "Win32" OR "${OFX_ARCH_NAME}" EQUAL "Win64")
    INSTALL(FILES libfftw3-3.dll
      DESTINATION GMIC.ofx.bundle/Contents/${OFX_ARCH_NAME}
    )
  ENDIF()

ENDIF()
