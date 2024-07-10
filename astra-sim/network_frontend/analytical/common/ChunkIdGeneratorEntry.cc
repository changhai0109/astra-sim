/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/ChunkIdGeneratorEntry.hh"
#include <cassert>

using namespace AstraSimAnalytical;

ChunkIdGeneratorEntry::ChunkIdGeneratorEntry() 
    : send_id(-1), recv_id(-1) {}

int ChunkIdGeneratorEntry::get_send_id() const {
  assert(send_id >= 0);

  return send_id;
}

int ChunkIdGeneratorEntry::get_recv_id() const {
  assert(recv_id >= 0);

  return recv_id;
}

void ChunkIdGeneratorEntry::increment_send_id() {
  send_id++;
}

void ChunkIdGeneratorEntry::increment_recv_id() {
  recv_id++;
}
