/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#pragma once

namespace AstraSimAnalytical {

/**
 * ChunkIdGeneratorEntry tracks the chunk id generated
 * for sim_send() and sim_recv() calls
 * per each (tag, src, dest, chunk_size) tuple.
 */
class ChunkIdGeneratorEntry {
 public:
  /**
   * Constructur.
   */
  ChunkIdGeneratorEntry();

  /**
   * Get the chunk id for sim_send() call.
   *
   * @return chunk id for sim_send() call
   */
  [[nodiscard]] int get_send_id() const;

  /**
   * Get the chunk id for sim_recv() call.
   *
   * @return chunk id for sim_recv() call
   */
  [[nodiscard]] int get_recv_id() const;

  /**
   * Increment the chunk id for sim_send() call.
   */
  void increment_send_id();

  /**
   * Increment the chunk id for sim_recv() call.
   */
  void increment_recv_id();

 private:
  /// current available chunk id for sim_send() call
  int send_id;

  /// current available chunk id for sim_recv() call
  int recv_id;
};

} // namespace AstraSimAnalytical
