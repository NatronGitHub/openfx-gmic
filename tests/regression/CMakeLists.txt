# Regression tests (ctest), built with GMIC_BUILD_TESTS. Each reproduces one past bug and names
# its issue, or the upstream issue and the local patch that works around it.

# pocketfft_unload (patches/pocketfft-no-thread-local.sh, https://github.com/NatronGitHub/openfx/issues/385,
# https://github.com/mreineck/pocketfft/pull/3): a module that includes the build's patched
# pocketfft_hdronly.h (GMIC_SRC_DIR) and runs its thread pool, built as GMIC.ofx is (MODULE, hidden
# visibility), gives the same FFTs with 1 and 8 threads, has no thread-local variables (macOS and
# Linux) and is unloaded by dlclose(). Its control, pocketfft_unload_unpatched, runs the same test
# on the module built with the unpatched header, which must report its thread-local variables: so
# the check can fail on both platforms. Not on Windows, where GMIC.ofx's pocketfft is
# single-threaded.
if(GMIC_FFT STREQUAL "pocketfft" AND NOT WIN32)
  find_package(Threads REQUIRED)
  # the plug-in's C++ standard (OFX_CXX_STANDARD, set by openfx's OpenFX.cmake)
  if(OFX_CXX_STANDARD)
    set(_std ${OFX_CXX_STANDARD})
  else()
    set(_std 17)
  endif()
  # the unpatched header (_pocketfft_src, checked against POCKETFFT_SHA256 in ../../CMakeLists.txt),
  # alone in its directory
  set(_unpatched_dir "${CMAKE_CURRENT_BINARY_DIR}/pocketfft-unpatched")
  configure_file("${_pocketfft_src}" "${_unpatched_dir}/pocketfft_hdronly.h" COPYONLY)
  foreach(_variant IN ITEMS patched unpatched)
    set(_module pocketfft_fft_module_${_variant})
    add_library(${_module} MODULE pocketfft_unload/pocketfft_fft_module.cpp)
    set_target_properties(${_module} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(_variant STREQUAL "patched")
      target_include_directories(${_module} PRIVATE "${GMIC_SRC_DIR}")
    else()
      target_include_directories(${_module} PRIVATE "${_unpatched_dir}")
    endif()
    target_compile_features(${_module} PRIVATE cxx_std_${_std})
    target_link_libraries(${_module} PRIVATE Threads::Threads)
  endforeach()
  add_executable(pocketfft_unload_test pocketfft_unload/pocketfft_unload_test.cpp)
  target_compile_features(pocketfft_unload_test PRIVATE cxx_std_${_std})
  target_link_libraries(pocketfft_unload_test PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(pocketfft_unload_test pocketfft_fft_module_patched pocketfft_fft_module_unpatched)
  add_test(NAME pocketfft_unload COMMAND pocketfft_unload_test "$<TARGET_FILE:pocketfft_fft_module_patched>")
  set_tests_properties(pocketfft_unload PROPERTIES TIMEOUT 120)
  if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME pocketfft_unload_unpatched COMMAND pocketfft_unload_test "$<TARGET_FILE:pocketfft_fft_module_unpatched>")
    set_tests_properties(pocketfft_unload_unpatched PROPERTIES TIMEOUT 120
      PASS_REGULAR_EXPRESSION "round 1: [^\n]* has thread-local variables")
  endif()
endif()
