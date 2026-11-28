# Regression tests (ctest), built with GMIC_BUILD_TESTS. Each reproduces one past bug and names
# its issue, or the upstream issue and the local patch that works around it.

# pocketfft_unload (patches/pocketfft-no-thread-local.sh, https://github.com/NatronGitHub/openfx/issues/385):
# a module that includes the build's patched pocketfft_hdronly.h (GMIC_SRC_DIR) and runs its thread
# pool, built as GMIC.ofx is (MODULE, hidden visibility), is unloaded by dlclose(), and has no
# thread-local variables on macOS. Not on Windows, where GMIC.ofx's pocketfft is single-threaded.
if(GMIC_FFT STREQUAL "pocketfft" AND NOT WIN32)
  find_package(Threads REQUIRED)
  add_library(pocketfft_fft_module MODULE pocketfft_unload/pocketfft_fft_module.cpp)
  set_target_properties(pocketfft_fft_module PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  target_include_directories(pocketfft_fft_module PRIVATE "${GMIC_SRC_DIR}")
  target_compile_features(pocketfft_fft_module PRIVATE cxx_std_17)
  target_link_libraries(pocketfft_fft_module PRIVATE Threads::Threads)
  add_executable(pocketfft_unload_test pocketfft_unload/pocketfft_unload_test.cpp)
  target_link_libraries(pocketfft_unload_test PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(pocketfft_unload_test pocketfft_fft_module)
  add_test(NAME pocketfft_unload COMMAND pocketfft_unload_test "$<TARGET_FILE:pocketfft_fft_module>")
  set_tests_properties(pocketfft_unload PROPERTIES TIMEOUT 120)
endif()
