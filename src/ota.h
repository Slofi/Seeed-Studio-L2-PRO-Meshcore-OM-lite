// SPDX-License-Identifier: MIT
#pragma once

namespace om {
// Call every loop(): watches the USB CDC stream for an upload request.
void otaPoll();
bool otaActive();
}
