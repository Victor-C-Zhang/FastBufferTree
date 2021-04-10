#include <gtest/gtest.h>
#include "../include/buffer_tree.h"

#define KB 1024

TEST(UtilTestSuite, TestInsertBasic) {
  // create a buffer tree with a buffer size of __, a branching factor
  // of two and based on a graph with 10 unique nodes [0-9]
  const int nodes = 10;
  const int num_updates = 400;

  BufferTree *buf_tree = new BufferTree("./test_", KB, 2, nodes);

  for (int i = 0; i < num_updates; i++) {
    update_t upd;
    upd.first.first = i % nodes;
    upd.first.second = (nodes - 1) - (i % nodes);
    upd.second = true;
    buf_tree->insert(upd);
  }

  // query for the 0 key at tag index 0
  data_ret_t ret = buf_tree->get_data(0, 0);
  uint32_t key = ret.first;
  std::vector<std::pair<uint32_t, bool>> updates = ret.second;
  // how many updates to we expect to see to each node
  int per_node = num_updates / nodes / nodes;

  ASSERT_EQ(0, key);

  int counts[nodes];
  for (int i = 0; i < nodes; i++) {
    counts[nodes] = 0;
  }

  for (std::pair<uint32_t, bool> upd : updates) {
    counts[upd.first]++;
  }

  for (int i = 0; i < nodes; i++) {
    ASSERT_EQ(per_node, counts[i]);
  }

  delete buf_tree;
}
