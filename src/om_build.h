// SPDX-License-Identifier: MIT
// EST-379: the build id the image carries. tools/build_id.py (a PlatformIO pre-build script) runs
// `git describe --always --dirty --abbrev=7` in the firmware folder and appends
// -D OM_BUILD_ID="<id>" to the build. This fallback keeps any non-PlatformIO compile (a host test,
// a syntax check) working; it is never used by a real image.
#pragma once

#ifndef OM_BUILD_ID
#define OM_BUILD_ID "unknown"
#endif
