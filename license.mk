# Licence of the build and FFT backend, for the top-level Makefile and GMIC_OFX/Makefile.
#
# LICENSE: GPL (default, or empty) or COMMERCIAL (Natron's NATRON_LICENSE, as in openfx-arena).
# COMMERCIAL builds use only code that can be distributed under the CeCILL-C option of G'MIC's
# dual licence: no FFTW (GPL), and a standard library without the community filters under the
# GPL or a CeCILL-only licence (see gmic_stdlib_commercial.txt).
#
# FFT: the library that computes G'MIC's Fourier transforms (CImg's own FFT only handles
# power-of-two image sizes, and fails on others):
# - fftw3: FFTW (GPL), the default of GPL builds;
# - mkl: the FFTW3 interface of Intel oneMKL (Intel Simplified Software License), linked
#   statically, found with pkg-config (MKL_PC, see GMIC_OFX/Makefile): the default of COMMERCIAL
#   builds on x86-64;
# - pocketfft: pocketfft (BSD-3-Clause, header-only, downloaded at a pinned commit), through
#   patches/cimg-pocketfft.sh: the default of COMMERCIAL builds on other architectures (arm64).
ifeq ($(LICENSE),)
  LICENSE := GPL
endif
ifneq ($(filter-out GPL COMMERCIAL,$(LICENSE)),)
  $(error LICENSE must be GPL or COMMERCIAL, not '$(LICENSE)')
endif
ifeq ($(LICENSE),COMMERCIAL)
  ifeq ($(FFT),)
    ifneq ($(filter x86_64 amd64 AMD64 x64,$(shell uname -m)),)
      FFT := mkl
    else
      FFT := pocketfft
    endif
  endif
  ifeq ($(FFT),fftw3)
    $(error LICENSE=COMMERCIAL cannot use FFT=fftw3 (FFTW is GPL): use FFT=mkl or FFT=pocketfft)
  endif
else
  ifeq ($(FFT),)
    FFT := fftw3
  endif
endif
ifneq ($(filter-out fftw3 mkl pocketfft,$(FFT)),)
  $(error FFT must be fftw3, mkl or pocketfft, not '$(FFT)')
endif
