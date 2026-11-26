#!/bin/bash
# Check that a Windows MSVC GMIC.ofx imports only KERNEL32, OPENGL32 (the point handles,
# openfx-supportext's overlays), SHELL32 and VCOMP140 (no C++ runtime,
# zlib, libpng, FFTW or libcurl DLL), with dumpbin (from an MSVC developer environment, under Git
# for Windows' bash). Usage: check_win_imports.sh <GMIC.ofx>
set -euo pipefail
ofx=${1:?usage: $0 <GMIC.ofx>}
deps=$(MSYS2_ARG_CONV_EXCL='*' dumpbin -nologo -dependents "$(cygpath -w "$ofx")" |
       sed -n '/has the following dependencies/,/Summary/p' | grep -iE '^ +[^ ]+\.dll$' | tr -d ' \r' | tr '[:lower:]' '[:upper:]' | sort | tr '\n' ' ')
echo "$ofx imports: $deps"
[ "$deps" = "KERNEL32.DLL OPENGL32.DLL SHELL32.DLL VCOMP140.DLL " ] ||
  { echo "FAIL: $ofx must import only KERNEL32, OPENGL32 (the point handles), SHELL32 and VCOMP140"; exit 1; }
