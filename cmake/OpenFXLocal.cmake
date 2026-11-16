# Stop-gap for the plug-in helpers that openfx's cmake/OpenFX.cmake is getting (NatronGitHub/openfx,
# "openfx CMake plan v1"), with the same names, arguments and options:
#   add_ofx_plugin(<TARGET> [INFO_PLIST <file>] [SOURCES <src>...])
#   ofx_plugin_resources(<TARGET> <file>...)
#   ofx_target_openmp(<TARGET>)        OFX_USE_OPENMP (default ON)
#   ofx_deploy_bundle(<TARGET>)        OFX_DEPLOY_BUNDLES (default OFF)
#   OFX_CXX_STANDARD (default 17), PLUGIN_INSTALLDIR
# Once the openfx submodule provides them, delete this file and the include(OpenFXLocal) line of
# CMakeLists.txt. Include it after add_subdirectory(openfx): it replaces openfx's add_ofx_plugin.

get_property(OFX_OPENFX_DIR GLOBAL PROPERTY OFX_PROJECT_SOURCE_DIR) # set by openfx's CMakeLists.txt
if(NOT OFX_OPENFX_DIR)
  message(FATAL_ERROR "OpenFXLocal.cmake: add_subdirectory(openfx) first")
endif()
include("${OFX_OPENFX_DIR}/cmake/OpenFX.cmake") # ARCHDIR, OFX_PLUGIN_BUILD_DIR

if(DEFINED CMAKE_CXX_STANDARD)
  set(_ofx_std_default ${CMAKE_CXX_STANDARD})
else()
  set(_ofx_std_default 17)
endif()
set(OFX_CXX_STANDARD ${_ofx_std_default} CACHE STRING "C++ standard of the plug-ins (17: Natron RB-2.6 Qt5, 20: Qt6)")
option(OFX_USE_OPENMP "Build the plug-ins with OpenMP (ofx_target_openmp)" ON)
option(OFX_DEPLOY_BUNDLES "Install step: copy the non-system libraries into the bundles (ofx_deploy_bundle)" OFF)

# add_ofx_plugin(<TARGET> [INFO_PLIST <file>] [SOURCES <src>...])
#
# Builds <TARGET>.ofx and assembles <TARGET>.ofx.bundle in ${OFX_PLUGIN_BUILD_DIR} (usable directly
# with OFX_PLUGIN_PATH), with the layout and flags of openfx/Examples/Makefile.master:
#   Contents/Info.plist              INFO_PLIST as is (default: openfx's Info.plist.in, configured)
#   Contents/<ARCHDIR>/<TARGET>.ofx  only the OFX entry points are exported (osxSymbols, linuxSymbols)
# and installs it into ${PLUGIN_INSTALLDIR} (a relative path is relative to CMAKE_INSTALL_PREFIX).
function(add_ofx_plugin TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 _ofx "" "INFO_PLIST" "SOURCES")
  if(APPLE)
    add_library(${TARGET} MODULE ${_ofx_SOURCES}) # a bundle (-bundle), as Makefile.master links it
  else()
    add_library(${TARGET} SHARED ${_ofx_SOURCES})
  endif()
  set_target_properties(${TARGET} PROPERTIES SUFFIX ".ofx" PREFIX ""
    CXX_STANDARD ${OFX_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
  target_link_libraries(${TARGET} PRIVATE openfx::Api)

  set(_bundle "${OFX_PLUGIN_BUILD_DIR}/${TARGET}.ofx.bundle")
  set_property(TARGET ${TARGET} PROPERTY OFX_BUNDLE_DIR "${_bundle}")
  if(_ofx_INFO_PLIST)
    get_filename_component(_plist "${_ofx_INFO_PLIST}" ABSOLUTE)
    configure_file("${_plist}" "${_bundle}/Contents/Info.plist" COPYONLY)
  else()
    set(PLUGIN_NAME ${TARGET})
    set(PLUGIN_EXE ${TARGET}.ofx)
    set(BUNDLE_IDENTIFIER org.openeffects.plugins.${TARGET})
    set(BUNDLE_SIGNATURE "????")
    set(PLUGIN_VERSION "1.0.0")
    configure_file("${OFX_OPENFX_DIR}/Examples/Info.plist.in" "${_bundle}/Contents/Info.plist")
  endif()
  set_target_properties(${TARGET} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_bundle}/Contents/${ARCHDIR}"
    RUNTIME_OUTPUT_DIRECTORY "${_bundle}/Contents/${ARCHDIR}")
  foreach(_cfg ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${_cfg}" _CFG)
    set_target_properties(${TARGET} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_bundle}/Contents/${ARCHDIR}"
      RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_bundle}/Contents/${ARCHDIR}")
  endforeach()

  # DEBUG in debug builds (NDEBUG comes with CMake's release flags)
  target_compile_definitions(${TARGET} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

  # Hidden symbols, as Makefile.master: only the OFX entry points are exported
  set_target_properties(${TARGET} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  set(_symbols "${OFX_OPENFX_DIR}/Examples/include")
  if(APPLE)
    target_link_options(${TARGET} PRIVATE "LINKER:-exported_symbols_list,${_symbols}/osxSymbols")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_symbols}/osxSymbols")
  elseif(NOT MSVC) # MinGW, Linux, FreeBSD; MSVC exports what OfxExport marks __declspec(dllexport)
    target_link_options(${TARGET} PRIVATE "LINKER:--version-script=${_symbols}/linuxSymbols")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_symbols}/linuxSymbols")
  endif()
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE /bigobj)
  elseif(MINGW)
    # COFF objects with more than 2^16 sections; Makefile.master's MinGW CPPFLAGS (math constants
    # in strict -std=c++NN modes, a C99 snprintf)
    target_compile_options(${TARGET} PRIVATE -Wa,-mbig-obj)
    target_compile_definitions(${TARGET} PRIVATE _USE_MATH_DEFINES __USE_MINGW_ANSI_STDIO=1)
  endif()

  # Makefile.master's runtime search paths, for libraries bundled with the plug-in (the build-tree
  # bundle gets them too, as with the Makefiles: osxDeploy.sh requires them)
  if(APPLE)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
  elseif(UNIX)
    set_target_properties(${TARGET} PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
  endif()
  set_target_properties(${TARGET} PROPERTIES BUILD_WITH_INSTALL_RPATH ON)

  set(_dest "${PLUGIN_INSTALLDIR}/${TARGET}.ofx.bundle/Contents")
  install(TARGETS ${TARGET}
    LIBRARY DESTINATION "${_dest}/${ARCHDIR}"
    RUNTIME DESTINATION "${_dest}/${ARCHDIR}")
  install(FILES "${_bundle}/Contents/Info.plist" DESTINATION "${_dest}")
endfunction()

# ofx_plugin_resources(<TARGET> <file>...): files of Contents/Resources (plug-in icons etc.)
function(ofx_plugin_resources TARGET)
  get_property(_bundle TARGET ${TARGET} PROPERTY OFX_BUNDLE_DIR)
  foreach(_res IN LISTS ARGN)
    get_filename_component(_abs "${_res}" ABSOLUTE)
    get_filename_component(_name "${_res}" NAME)
    configure_file("${_abs}" "${_bundle}/Contents/Resources/${_name}" COPYONLY)
    install(FILES "${_abs}" DESTINATION "${PLUGIN_INSTALLDIR}/${TARGET}.ofx.bundle/Contents/Resources")
  endforeach()
endfunction()

# ofx_target_openmp(<TARGET>): compile and link with OpenMP (OpenMP::OpenMP_CXX), if OFX_USE_OPENMP.
# Apple clang has no OpenMP runtime: libomp is looked for in OpenMP_ROOT, LIBOMP_PREFIX (default
# /opt/local, MacPorts: include/libomp and lib/libomp, as Makefile.master's LIBOMP_PREFIX), and
# Homebrew's libomp, unless OpenMP_CXX_FLAGS etc. are given.
function(ofx_target_openmp TARGET)
  if(NOT OFX_USE_OPENMP)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND NOT OpenMP_CXX_FLAGS)
    set(LIBOMP_PREFIX "/opt/local" CACHE PATH "Apple clang: prefix of the OpenMP runtime (libomp)")
    set(_hints ${OpenMP_ROOT} $ENV{OpenMP_ROOT} "${LIBOMP_PREFIX}" /opt/homebrew/opt/libomp /usr/local/opt/libomp)
    find_path(LIBOMP_INCLUDE_DIR omp.h HINTS ${_hints} PATH_SUFFIXES include/libomp include NO_DEFAULT_PATH)
    find_library(LIBOMP_LIBRARY omp HINTS ${_hints} PATH_SUFFIXES lib/libomp lib NO_DEFAULT_PATH)
    if(LIBOMP_INCLUDE_DIR AND LIBOMP_LIBRARY)
      set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_INCLUDE_DIR}" CACHE STRING "" FORCE)
      set(OpenMP_CXX_LIB_NAMES "omp" CACHE STRING "" FORCE)
      set(OpenMP_omp_LIBRARY "${LIBOMP_LIBRARY}" CACHE FILEPATH "" FORCE)
    endif()
  endif()
  find_package(OpenMP REQUIRED COMPONENTS CXX)
  target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
endfunction()

# ofx_deploy_bundle(<TARGET>): if OFX_DEPLOY_BUNDLES, the install step makes the installed bundle
# self-contained. macOS: osxDeploy.sh (as Makefile.master does for release builds: the non-system
# libraries are copied into Contents/Libraries and referred to through @rpath; needs gsed), then an
# ad hoc signature. Other systems: not implemented in this stop-gap.
function(ofx_deploy_bundle TARGET)
  if(NOT OFX_DEPLOY_BUNDLES)
    return()
  endif()
  if(NOT APPLE)
    message(WARNING "ofx_deploy_bundle: only implemented on macOS in this stop-gap (cmake/OpenFXLocal.cmake)")
    return()
  endif()
  if(IS_ABSOLUTE "${PLUGIN_INSTALLDIR}")
    set(_root "${PLUGIN_INSTALLDIR}")
  else()
    set(_root "\${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}")
  endif()
  # install_name_tool invalidates the signatures: sign each Mach-O file again (ad hoc), without
  # --deep, which wants a CFBundleIdentifier
  install(CODE "
    set(_b \"\$ENV{DESTDIR}${_root}/${TARGET}.ofx.bundle\")
    execute_process(COMMAND bash \"${OFX_OPENFX_DIR}/Examples/include/osxDeploy.sh\" \"\${_b}\" ${TARGET}.ofx
                    RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR \"osxDeploy.sh failed on \${_b}\")
    endif()
    file(GLOB _libs \"\${_b}/Contents/Libraries/*.dylib\")
    foreach(_f IN LISTS _libs ITEMS \"\${_b}/Contents/MacOS/${TARGET}.ofx\")
      execute_process(COMMAND codesign --force --sign - \"\${_f}\" RESULT_VARIABLE _r)
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR \"codesign failed on \${_f}\")
      endif()
    endforeach()
  ")
endfunction()
