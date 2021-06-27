#include <gtest/gtest.h>
#include <math.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "../include/buffer_tree.h"

#define KB (uint64_t (1 << 10))
#define MB (uint64_t (1 << 20))
#define GB (uint64_t (1 << 30))

static bool shutdown = false;
static std::atomic<uint64_t> upd_processed;

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

void progress(const uint64_t num_updates) {
    while(true) {
        sleep(5);
        uint64_t cur = upd_processed.load();
        printf("number of insertions processed: %lu %f%% \r", cur, cur/((double)num_updates/100));

        fflush(stdout);
        if (upd_processed == num_updates) {
            printf("number of insertions processed: DONE         \n");
            break;
        }
    }
}

// helper function to run a basic test of the buffer tree with
// various parameters
// this test only works if the depth of the tree does not exceed 1
// and no work is claimed off of the work queue
// to work correctly num_updates must be a multiple of nodes
void run_test(const int nodes, const uint64_t num_updates, const uint64_t buffer_size, 
 const int branch_factor, const int threads=1) {
    printf("Running Test: nodes=%i num_updates=%lu buffer_size %lu branch_factor %i\n",
         nodes, num_updates, buffer_size, branch_factor);

    BufferTree *buf_tree = new BufferTree("./test_", buffer_size, branch_factor, nodes, threads, 16);
    shutdown = false;
    upd_processed = 0;

    std::thread query_threads[threads];
    for (int t = 0; t < threads; t++) {
        query_threads[t] = std::thread(querier, buf_tree, nodes);
    }
    std::thread progress_thr(progress, num_updates);

    auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < num_updates; i++) {
        update_t upd;
        upd.first = i % nodes;
        upd.second = (nodes - 1) - (i % nodes);
        buf_tree->insert(upd);
    }
    std::chrono::duration<double> delta = std::chrono::steady_clock::now() - start;
    printf("insertions took %f seconds: average rate = %f\n", delta.count(), num_updates/delta.count());
    buf_tree->force_flush();
    shutdown = true;
    buf_tree->set_non_block(true); // tell any waiting threads to reset

    delta = std::chrono::steady_clock::now() - start;
    printf("insert+force_flush took %f seconds: average rate = %f\n", delta.count(), num_updates/delta.count());

    for (int t = 0; t < threads; t++) {
        query_threads[t].join();
    }
    progress_thr.join();
    delete buf_tree;
}

TEST(Experiment, LargeStandard) {
    const int nodes            = 512;
    const uint64_t num_updates = MB << 5;
    const uint64_t buf         = MB;
    const int branch           = 8;

    run_test(nodes, num_updates, buf, branch);
}

TEST(Experiment, LargeWide) {
    const int nodes            = 512;
    const uint64_t num_updates = MB << 5;
    const uint64_t buf         = MB;
    const int branch           = 16;

    run_test(nodes, num_updates, buf, branch);
}

TEST(Experiment, ExtraLarge) {
    const int nodes            = 1024;
    const uint64_t num_updates = MB << 8;
    const uint64_t buf         = MB << 1;
    const int branch           = 16;

    run_test(nodes, num_updates, buf, branch);
}

TEST(SteadyState, HugeExperiment) {
    const int nodes            = 250000;
    const uint64_t num_updates = GB << 2; // 8 billion
    const uint64_t buf         = MB;
    const int branch           = 64;
    const int threads          = 10;

    run_test(nodes, num_updates, buf, branch, threads);
}

TEST(SteadyState, BigFanoutExperiment) {
    const int nodes            = 250000;
    const uint64_t num_updates = GB << 2; // 8 billion
    const uint64_t buf         = MB;
    const int branch           = 512;
    const int threads          = 10;

    run_test(nodes, num_updates, buf, branch, threads);
}