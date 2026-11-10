// SPDX-License-Identifier: MIT
// Build shim, not new MeshCore code.
//
// lib/meshcore/library.json was written for the old core-only build and excludes
// helpers/CommonRadioPrefs.cpp. The companion code needs it (MyMesh::handleCommand and the
// NodePrefs::RadioPrefs vtable: setByKey/getByKey). We must not edit lib/meshcore, so this
// translation unit compiles the upstream file unchanged through the include below.
//
// If a later update changes lib/meshcore/library.json to build CommonRadioPrefs.cpp itself,
// delete this file.
#include "../lib/meshcore/helpers/CommonRadioPrefs.cpp"
