# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Release-only x64-windows triplet for CI. Identical to vcpkg's built-in x64-windows,
# but VCPKG_BUILD_TYPE=release skips the debug build. The default triplet builds every
# dependency TWICE (x64-windows-dbg + x64-windows-rel); CI never needs debug grpc, so
# building release only roughly halves both the vcpkg build time and its peak disk use.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
