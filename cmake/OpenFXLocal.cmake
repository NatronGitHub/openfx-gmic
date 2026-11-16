# Stop-gap for the plug-in helpers of openfx's cmake/OpenFX.cmake (NatronGitHub/openfx#125), with the
# same names, arguments and options, for the subset that GMIC uses:
#   add_ofx_plugin(<TARGET> [INFO_PLIST <file>] [SOURCES <src>...])
#   ofx_plugin_resources(<TARGET> [DESTINATION <subdir>] <file>...)
#   ofx_target_openmp(<TARGET> [REQUIRED])   OFX_USE_OPENMP (default ON), OFX_LIBOMP_ROOT
#   ofx_deploy_bundle(<TARGET>)              OFX_DEPLOY_BUNDLES (default OFF; macOS only)
#   OFX_CXX_STANDARD (default 17), PLUGIN_INSTALLDIR (default OFX/Plugins, relative to the prefix)
# Once the openfx submodule provides them, delete this file and the include(OpenFXLocal) line of
# CMakeLists.txt. Include it after add_subdirectory(openfx): it replaces openfx's add_ofx_plugin.

get_property(OFX_OPENFX_DIR GLOBAL PROPERTY OFX_PROJECT_SOURCE_DIR) # set by openfx's CMakeLists.txt
if(NOT OFX_OPENFX_DIR)
  message(FATAL_ERROR "OpenFXLocal.cmake: add_subdirectory(openfx) first")
endif()
if(NOT PLUGIN_INSTALLDIR)
  set(PLUGIN_INSTALLDIR "OFX/Plugins" CACHE STRING "Where the OFX bundles are installed (relative: to CMAKE_INSTALL_PREFIX)" FORCE)
endif()
include("${OFX_OPENFX_DIR}/cmake/OpenFX.cmake") # ARCHDIR, OFX_PLUGIN_BUILD_DIR

if(DEFINED CMAKE_CXX_STANDARD)
  set(_ofx_std_default ${CMAKE_CXX_STANDARD})
else()
  set(_ofx_std_default 17)
endif()
set(OFX_CXX_STANDARD ${_ofx_std_default} CACHE STRING "C++ standard of the plug-ins built with add_ofx_plugin")
option(OFX_USE_OPENMP "ofx_target_openmp(): use OpenMP" ON)
option(OFX_DEPLOY_BUNDLES "ofx_deploy_bundle(): make the installed bundles self-contained" OFF)

# add_ofx_plugin(<TARGET> [INFO_PLIST <file>] [SOURCES <src>...])
#
# Builds <TARGET>.ofx and assembles <TARGET>.ofx.bundle in ${OFX_PLUGIN_BUILD_DIR} (usable directly
# with OFX_PLUGIN_PATH), with the layout and flags of openfx/Examples/Makefile.master:
#   Contents/Info.plist              INFO_PLIST, configured with @ONLY (default: openfx's
#                                    Info.plist.in); copied into the bundle at each build
#   Contents/<ARCHDIR>/<TARGET>.ofx  only the OFX entry points are exported (osxSymbols, linuxSymbols)
# and installs it into ${PLUGIN_INSTALLDIR}. Link the target with openfx::Api or openfx::Support.
function(add_ofx_plugin TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 _ofx "" "INFO_PLIST" "SOURCES")
  if(APPLE)
    add_library(${TARGET} MODULE ${_ofx_SOURCES}) # a bundle (-bundle), as Makefile.master links it
  else()
    add_library(${TARGET} SHARED ${_ofx_SOURCES})
  endif()
  set_target_properties(${TARGET} PROPERTIES SUFFIX ".ofx" PREFIX ""
    CXX_STANDARD ${OFX_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

  set(_bundle "${OFX_PLUGIN_BUILD_DIR}/${TARGET}.ofx.bundle")
  set_target_properties(${TARGET} PROPERTIES OFX_BUNDLE_DIR "${_bundle}" OFX_BUNDLE_NAME "${TARGET}")
  set(PLUGIN_NAME ${TARGET})
  set(PLUGIN_EXE ${TARGET}.ofx)
  set(BUNDLE_IDENTIFIER org.openeffects.plugins.${TARGET})
  set(BUNDLE_SIGNATURE "????")
  set(PLUGIN_VERSION "1.0.0")
  set(_plist "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.Info.plist")
  if(_ofx_INFO_PLIST)
    get_filename_component(_in "${_ofx_INFO_PLIST}" ABSOLUTE)
    configure_file("${_in}" "${_plist}" @ONLY)
  else()
    configure_file("${OFX_OPENFX_DIR}/Examples/Info.plist.in" "${_plist}")
  endif()
  # copied at each build, so that a bundle removed from the build tree is complete again
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_plist}" "${_bundle}/Contents/Info.plist"
    VERBATIM)
  set_target_properties(${TARGET} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_bundle}/Contents/${ARCHDIR}"
    RUNTIME_OUTPUT_DIRECTORY "${_bundle}/Contents/${ARCHDIR}")
  foreach(_cfg ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${_cfg}" _CFG)
    set_target_properties(${TARGET} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${_bundle}/Contents/${ARCHDIR}"
      RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_bundle}/Contents/${ARCHDIR}")
  endforeach()

  target_compile_definitions(${TARGET} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

  # Hidden symbols, as Makefile.master: only the OFX entry points are exported
  set_target_properties(${TARGET} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  set(_symbols "${OFX_OPENFX_DIR}/Examples/include")
  if(APPLE)
    target_link_options(${TARGET} PRIVATE "LINKER:-exported_symbols_list,${_symbols}/osxSymbols")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_symbols}/osxSymbols")
  elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|FreeBSD")
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
  # bundle gets them too, as with the Makefiles)
  if(APPLE)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
  elseif(UNIX)
    set_target_properties(${TARGET} PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
  endif()
  set_target_properties(${TARGET} PROPERTIES BUILD_WITH_INSTALL_RPATH ON)

  # Windows: the import library (libGMIC.dll.a, GMIC.lib) isn't part of the bundle
  set(_dest "${PLUGIN_INSTALLDIR}/${TARGET}.ofx.bundle/Contents")
  install(TARGETS ${TARGET}
    LIBRARY DESTINATION "${_dest}/${ARCHDIR}"
    RUNTIME DESTINATION "${_dest}/${ARCHDIR}"
    ARCHIVE DESTINATION lib COMPONENT ofx_import_libraries EXCLUDE_FROM_ALL)
  install(FILES "${_plist}" DESTINATION "${_dest}" RENAME Info.plist)
endfunction()

# ofx_plugin_resources(<TARGET> [DESTINATION <subdir>] <file>...): files of Contents/Resources
# (icons, presets...), copied into the build-tree bundle at each build, and installed
function(ofx_plugin_resources TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 _ofx "" "DESTINATION" "")
  get_target_property(_bundle ${TARGET} OFX_BUNDLE_DIR)
  set(_sub "Resources")
  if(_ofx_DESTINATION)
    set(_sub "Resources/${_ofx_DESTINATION}")
  endif()
  set(_files "")
  foreach(_f IN LISTS _ofx_UNPARSED_ARGUMENTS)
    get_filename_component(_abs "${_f}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _files "${_abs}")
  endforeach()
  if(NOT _files)
    return()
  endif()
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bundle}/Contents/${_sub}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_files} "${_bundle}/Contents/${_sub}"
    VERBATIM)
  install(FILES ${_files} DESTINATION "${PLUGIN_INSTALLDIR}/${TARGET}.ofx.bundle/Contents/${_sub}")
endfunction()

# ofx_target_openmp(<TARGET> [REQUIRED]): compile and link with OpenMP (OpenMP::OpenMP_CXX), if
# OFX_USE_OPENMP. Apple clang has no OpenMP runtime: libomp is taken from OFX_LIBOMP_ROOT (a prefix,
# or the directories of omp.h and libomp) or MacPorts (/opt/local/include/libomp,
# /opt/local/lib/libomp), unless OpenMP_CXX_FLAGS etc. are given.
function(ofx_target_openmp TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 _ofx "REQUIRED" "" "")
  if(NOT OFX_USE_OPENMP)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND NOT OpenMP_CXX_FLAGS AND NOT TARGET OpenMP::OpenMP_CXX)
    # search again when OFX_LIBOMP_ROOT changes
    if(NOT "${OFX_LIBOMP_ROOT}" STREQUAL "${_OFX_LIBOMP_ROOT_SEARCHED}")
      unset(OFX_LIBOMP_INCLUDE_DIR CACHE)
      unset(OFX_LIBOMP_LIBRARY CACHE)
      set(_OFX_LIBOMP_ROOT_SEARCHED "${OFX_LIBOMP_ROOT}" CACHE INTERNAL "")
    endif()
    find_path(OFX_LIBOMP_INCLUDE_DIR omp.h
      HINTS ${OFX_LIBOMP_ROOT} ${OFX_LIBOMP_ROOT}/include/libomp ${OFX_LIBOMP_ROOT}/include
            /opt/local/include/libomp
      NO_DEFAULT_PATH)
    find_library(OFX_LIBOMP_LIBRARY omp
      HINTS ${OFX_LIBOMP_ROOT} ${OFX_LIBOMP_ROOT}/lib/libomp ${OFX_LIBOMP_ROOT}/lib /opt/local/lib/libomp
      NO_DEFAULT_PATH)
    if(OFX_LIBOMP_INCLUDE_DIR AND OFX_LIBOMP_LIBRARY)
      # normal variables, not cached: they follow OFX_LIBOMP_ROOT
      set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${OFX_LIBOMP_INCLUDE_DIR}")
      set(OpenMP_CXX_LIB_NAMES omp)
      set(OpenMP_omp_LIBRARY "${OFX_LIBOMP_LIBRARY}")
    endif()
  endif()
  find_package(OpenMP COMPONENTS CXX)
  if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
  elseif(_ofx_REQUIRED)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
      set(_hint "Apple clang needs libomp: sudo port install libomp, or -DOFX_LIBOMP_ROOT=<dir>")
    else()
      set(_hint "the compiler has no OpenMP support")
    endif()
    message(FATAL_ERROR "ofx_target_openmp(${TARGET}): OpenMP not found (${_hint}); "
      "configure with -DOFX_USE_OPENMP=OFF to build without it")
  else()
    message(STATUS "ofx_target_openmp(${TARGET}): OpenMP not found, building without it")
  endif()
endfunction()

# ofx_deploy_bundle(<TARGET>): if OFX_DEPLOY_BUNDLES, the install step makes the installed bundle
# self-contained. macOS: osxDeploy.sh (as Makefile.master does for release builds: the non-system
# libraries are copied into Contents/Libraries and referred to through @rpath; needs gsed), then
# ad hoc signatures, and the install fails if a Mach-O file still references /opt/local, /usr/local
# or /opt/homebrew. Other systems: not implemented.
function(ofx_deploy_bundle TARGET)
  if(NOT OFX_DEPLOY_BUNDLES)
    return()
  endif()
  if(NOT APPLE)
    message(WARNING "ofx_deploy_bundle: only implemented on macOS")
    return()
  endif()
  if(IS_ABSOLUTE "${PLUGIN_INSTALLDIR}")
    set(_root "${PLUGIN_INSTALLDIR}")
  else()
    set(_root "\${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}")
  endif()
  install(CODE "
    set(_b \"\$ENV{DESTDIR}${_root}/${TARGET}.ofx.bundle\")
    execute_process(COMMAND bash \"${OFX_OPENFX_DIR}/Examples/include/osxDeploy.sh\" \"\${_b}\" ${TARGET}.ofx
                    RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR \"osxDeploy.sh failed on \${_b}\")
    endif()
    file(GLOB _libs \"\${_b}/Contents/Libraries/*\")
    foreach(_f IN LISTS _libs ITEMS \"\${_b}/Contents/MacOS/${TARGET}.ofx\")
      execute_process(COMMAND codesign --force --sign - \"\${_f}\" RESULT_VARIABLE _r)
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR \"codesign failed on \${_f}\")
      endif()
      execute_process(COMMAND otool -L \"\${_f}\" OUTPUT_VARIABLE _deps)
      if(_deps MATCHES \"\\t(/opt/local|/usr/local|/opt/homebrew)/\")
        message(FATAL_ERROR \"\${_f} still references \${CMAKE_MATCH_1}:\\n\${_deps}\")
      endif()
    endforeach()
    execute_process(COMMAND codesign --force --sign - \"\${_b}\" RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR \"codesign failed on \${_b}\")
    endif()
  ")
endfunction()
