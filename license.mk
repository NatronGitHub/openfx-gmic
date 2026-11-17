# Licence of the build and FFT backend, for the top-level Makefile and GMIC_OFX/Makefile.
#
# LICENSE: GPL (default, or empty) or COMMERCIAL (Natron's NATRON_LICENSE, as in openfx-arena).
# COMMERCIAL builds use only code that can be distributed under the CeCILL-C option of G'MIC's
# dual licence: no FFTW (GPL), and a standard library without the community filters under the
# GPL or a CeCILL-only licence (see gmic_stdlib_commercial.txt).
#
# FFT: the library that computes G'MIC's Fourier transforms (CImg's own FFT only handles
# power-of-two image sizes, and fails on others):
# - fftw3: FFTW (GPL), for GPL builds;
# - pocketfft: pocketfft (BSD-3-Clause, header-only, downloaded at a pinned commit), through
#   patches/cimg-pocketfft.sh (applied only for FFT=pocketfft), for COMMERCIAL builds. GPL builds
#   may use it too (FFT=pocketfft), e.g. to test it.
ifeq ($(LICENSE),)
  LICENSE := GPL
endif
ifneq ($(filter-out GPL COMMERCIAL,$(LICENSE)),)
  $(error LICENSE must be GPL or COMMERCIAL, not '$(LICENSE)')
endif
ifeq ($(LICENSE),COMMERCIAL)
  ifeq ($(FFT),)
    FFT := pocketfft
  endif
  ifneq ($(FFT),pocketfft)
    $(error LICENSE=COMMERCIAL needs FFT=pocketfft, not '$(FFT)' (FFTW is GPL))
  endif
else
  ifeq ($(FFT),)
    FFT := fftw3
  endif
endif
ifneq ($(filter-out fftw3 pocketfft,$(FFT)),)
  $(error FFT must be fftw3 or pocketfft, not '$(FFT)')
endif
