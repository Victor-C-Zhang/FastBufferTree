#include <gtest/gtest.h>
#include <math.h>
#include <thread>
#include <atomic>
#include "../include/buffer_tree.h"

#define KB 1 << 10
#define MB 1 << 20
#define GB 1 << 30

static bool shutdown = false;
static std::atomic<uint32_t> upd_processed;

// queries the buffer tree and verifies that the data
// returned makes sense
// Should be run in a seperate thread
void querier(BufferTree *buf_tree, int nodes) {
  data_ret_t data;
  while(true) {
    bool valid = buf_tree->get_data(data);
    if (valid) {
      Node key = data.first;
      std::vector<Node> updates = data.second;
      // verify that the updates are all between the correct nodes
      for (Node upd : updates) {
        // printf("edge to %d\n", upd.first);
        ASSERT_EQ(nodes - (key + 1), upd) << "key " << key;
        upd_processed += 1;
      }
    }
    else if(shutdown)
      return;
  }
}

// helper function to run a basic test of the buffer tree with
// various parameters
// this test only works if the depth of the tree does not exceed 1
// and no work is claimed off of the work queue
// to work correctly num_updates must be a multiple of nodes
void run_test(const int nodes, const int num_updates, const int buffer_size, const int branch_factor) {
  printf("Running Test: nodes=%i num_updates=%i buffer_size %i branch_factor %i\n",
         nodes, num_updates, buffer_size, branch_factor);

  BufferTree *buf_tree = new BufferTree("./test_", buffer_size, branch_factor, nodes, 1, true);
  shutdown = false;
  upd_processed = 0;
  std::thread qworker(querier, buf_tree, nodes);

  for (int i = 0; i < num_updates; i++) {
    update_t upd;
    upd.first = i % nodes;
    upd.second = (nodes - 1) - (i % nodes);
    buf_tree->insert(upd);
  }
  buf_tree->force_flush();
  shutdown = true;
  buf_tree->set_non_block(true); // switch to non-blocking calls in an effort to exit

  qworker.join();
  ASSERT_EQ(num_updates, upd_processed);
  delete buf_tree;
}

TEST(BasicInsert, Small) {
  const int nodes = 10;
  const int num_updates = 400;
  const int buf = KB << 2;
  const int branch = 2;

  run_test(nodes, num_updates, buf, branch);
}

TEST(BasicInsert, Medium) {
  const int nodes = 100;
  const int num_updates = 360000;
  const int buf = MB;
  const int branch = 8;

  run_test(nodes, num_updates, buf, branch);
}

// test where we fill the lowest buffers as full as we can
// with insertions.
TEST(BasicInsert, FillLowest) {
  uint updates = (8 * MB) / BufferTree::serial_update_size;
  updates -= updates % 8 + 8;

  const int nodes = 8;
  const int num_updates = updates;
  const int buf = MB;
  const int branch = 2;

  run_test(nodes, num_updates, buf, branch);
}

// test designed to trigger recursive flushes
// We will do this by inserting to all nodes until the 
// root buffer has become full 15 times. This will result
// in every internal node (but not the root) being half full
//
// Then we will insert a full buffer of updates for each node
// causing a large number of cascading flushes
TEST(BasicInsert, EvilInsertions) {
  int full_root = MB/BufferTree::serial_update_size;
  const int nodes = 32;
  const int round_1_size = 15 * full_root; // enough updates to fill it up half way
  const int num_updates = 32 * full_root + round_1_size;
  const int buf = MB;
  const int branch = 2;

  BufferTree *buf_tree = new BufferTree("./test_", buf, branch, nodes, 1, true);
  shutdown = false;
  upd_processed = 0;
  std::thread qworker(querier, buf_tree, nodes);

  for (int i = 0; i < round_1_size; i++) {
    update_t upd;
    upd.first = i % nodes;
    upd.second = (nodes - 1) - (i % nodes);
    buf_tree->insert(upd);
  }
  for(int i = 0; i < nodes; i++) {
    for (int n = 0; n < full_root; n++) {
      update_t upd;
      upd.first = i % nodes;
      upd.second = (nodes - 1) - (i % nodes);
      buf_tree->insert(upd);
    }
  }
  buf_tree->force_flush();
  shutdown = true;
  buf_tree->set_non_block(true); // switch to non-blocking calls in an effort to exit

  qworker.join();
  ASSERT_EQ(num_updates, upd_processed);
  delete buf_tree;
}

TEST(Parallelism, ManyQueryThreads) {
  const int nodes = 1024;
  const int num_updates = 5206;
  const int buf = MB;
  const int branch = 2;

  // here we limit the number of slots in the circular queue to 
  // create contention between the threads. (we pass 5 instead of 20)
  BufferTree *buf_tree = new BufferTree("./test_", buf, branch, nodes, 5, true);
  shutdown = false;
  upd_processed = 0;
  std::thread query_threads[20];
  for (int t = 0; t < 20; t++) {
    query_threads[t] = std::thread(querier, buf_tree, nodes);
  }
  
  for (int i = 0; i < num_updates; i++) {
    update_t upd;
    upd.first = i % nodes;
    upd.second = (nodes - 1) - (i % nodes);
    buf_tree->insert(upd);
  }
  buf_tree->force_flush();
  shutdown = true;
  buf_tree->set_non_block(true); // switch to non-blocking calls in an effort to exit

  for (int t = 0; t < 20; t++) {
    query_threads[t].join();
  }
  ASSERT_EQ(num_updates, upd_processed);
  delete buf_tree;
}
