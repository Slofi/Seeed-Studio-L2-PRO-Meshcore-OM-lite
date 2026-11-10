// SPDX-License-Identifier: MIT
// Include shim for MeshCore's companion code.
//
// MyMesh.h has `#include <RTClib.h>`, but neither MyMesh.cpp nor MyMesh.h uses anything from
// it (checked at v1.18.0: no DateTime, no RTC_DS3231). Our board has no RTC chip; time comes
// from the timekeeper. The sandbox has no network, so instead of pulling the full Adafruit
// RTClib dependency for an unused include this empty header lets the unmodified upstream file
// compile. If a later MeshCore release starts using RTClib, add the real library then.
#pragma once
