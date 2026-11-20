# Arm64EC with static libraries and the static C runtime (/MT), as x64-windows-static and
# arm64-windows-static: vcpkg has only a dynamic arm64ec-windows community triplet.
set(VCPKG_TARGET_ARCHITECTURE arm64ec)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
