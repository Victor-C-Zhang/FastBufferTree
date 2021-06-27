#include <gtest/gtest.h>
#include <math.h>
#include <thread>
#include <atomic>
#include "../include/buffer_tree.h"

#define KB (1 << 10)
#define MB (1 << 20)
#define GB (1 << 30)

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

  BufferTree *buf_tree = new BufferTree("./test_", buffer_size, branch_factor, nodes, 1, 8);
  shutdown = false;
  upd_processed = 0;
  std::thread qworker(querier, buf_tree, nodes);

  for (int i = 0; i < num_updates; i++) {
    update_t upd;
    upd.first = i % nodes;
    upd.second = (nodes - 1) - (i % nodes);
    buf_tree->insert(upd);
  }
  printf("force flush\n");
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

TEST(BasicInsert, ManyInserts) {
  const int nodes = 32;
  const int num_updates = 1000000;
  const int buf = MB;
  const int branch = 2;

  run_test(nodes, num_updates, buf, branch);
}

// test designed to trigger recursive flushes
// Insert full root buffers which are 95% node 0 and 5% a node
// which will make 95% split from 5% at different levels of 
// the tree. We do this process from bottom to top. When this
// is done. We insert a full buffer of 0 updates.
//
// For exampele 0 and 2, then 0 and 4, etc. 
TEST(BasicInsert, EvilInsertions) {
  int full_root = MB/BufferTree::serial_update_size;
  const int nodes = 32;
  const int num_updates = 4 * full_root;
  const int buf = MB;
  const int branch = 2;

  BufferTree *buf_tree = new BufferTree("./test_", buf, branch, nodes, 1, 8);
  shutdown = false;
  upd_processed = 0;
  std::thread qworker(querier, buf_tree, nodes);

  for (int l = 1; l <= 3; l++) {
    for (int i = 0; i < full_root; i++) {
      update_t upd;
      if (i < .95 * full_root) {
        upd.first  = 0;
        upd.second = (nodes - 1) - (0 % nodes);
      } else {
        upd.first  = 1 << l;
        upd.second = (nodes - 1) - (upd.first % nodes);
      }
      buf_tree->insert(upd);
    }
  }
  
  for (int n = 0; n < full_root; n++) {
    update_t upd;
    upd.first = 0;
    upd.second = (nodes - 1) - (0 % nodes);
    buf_tree->insert(upd);
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
  // create contention between the threads. (we pass 5,1 instead of 20,8)
  BufferTree *buf_tree = new BufferTree("./test_", buf, branch, nodes, 5, 1);
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
