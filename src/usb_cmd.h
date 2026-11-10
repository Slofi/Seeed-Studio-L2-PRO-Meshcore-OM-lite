// SPDX-License-Identifier: MIT
// Bridge between the USB frame reader (ota.cpp) and the shell (main.cpp).
//
// ota.cpp owns the USB parser and knows the framing only; main.cpp owns the screens, the mesh
// facade and the touch path. One function crosses the line: the shell executes the command and
// writes the single final answer line. Data lines for `nodes` and `msgs` are printed by the shell
// directly, before that line.
#pragma once

#include <stddef.h>

namespace om {

// `text` is the frame's buffer and is edited in place by the parser. `reply` always receives one
// complete line, terminated, without a newline: "[cmd] ok ..." or "[cmd] error <reason>".
void usbCommandExecute(char* text, char* reply, size_t cap);

}  // namespace om
