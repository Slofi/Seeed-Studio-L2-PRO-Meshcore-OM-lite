// SPDX-License-Identifier: MIT
// EST-371: the small read-only view of the shell's state that the USB command module needs. The
// shell owns the buffers; a command never writes them, it only prints what the screens show.
#pragma once

#include <stdint.h>

#include "board.h"
#include "history_store.h"
#include "mesh_link.h"
#include "ui/screen_chat.h"
#include "ui/screen_nodes.h"

namespace om {
namespace app {

int nodeCount();
const ui::NodeItem& node(int i);
void refreshNodesNow();
int unreadTotal();

void refreshChatsNow();
int chatChannel(int chat);
int chatThread(int chat);
int chatCount();
const ui::ChatEntry& chat(int i);
int chatSel();
int viewCount();
const history::Msg& viewMsg(int i);
bool viewLoading();
int viewChat();

MeshDm* dmScratch();

struct ChatShownInfo {
  const char* key = "";
  int shown = -1;
  uint32_t openUs = 0;
  uint32_t windows = 0;
  uint32_t newestLogId = 0;
  uint32_t newestWall = 0;
  uint32_t newestUp = 0;
  bool newestReal = false;
};
ChatShownInfo chatShown(int i);

}  // namespace app
}  // namespace om
