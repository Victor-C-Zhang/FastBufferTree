#ifndef FASTBUFFERTREE_BUFFER_TREE_H
#define FASTBUFFERTREE_BUFFER_TREE_H

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <math.h>
#include "update.h"
#include "buffer_control_block.h"
#include "circular_queue.h"

typedef void insert_ret_t;
typedef void flush_ret_t;
typedef std::pair<Node, std::vector<Node>> data_ret_t;

/*
 * Quick and dirty buffer tree skeleton.
 * Metadata about buffers (buffer control blocks) will be stored in memory.
 * The root buffer will be stored in memory.
 * All other buffers will be primarily stored on disk.
 * The tree operates read-lazily, only reading (non-root) buffers into memory
 *    if they need to be flushed.
 * Flushing and flush-related work is handled by a dynamic thread pool.
 * A flush queue will be maintained, from which threads pick tasks.
 * DESIGN NOTE: Currently, a buffer cannot flush to another buffer that needs to
 * be flushed. This is to prevent starvation.
 */
class BufferTree {
private:
  // root directory of tree
  std::string dir;

  // size of a buffer (leaf buffers will likely be smaller)
  uint32_t M;

  // branching factor
  uint32_t B;

  // number of nodes in the graph
  Node N;

  // metadata control block(s)
  // level 1 blocks take indices 0->(B-1). So on and so forth from there
  std::vector<BufferControlBlock*> buffers;

  // buffers which we will use when performing flushes
  // we maintain these for every level of the tree 
  // to handle recursive flushing.
  // TODO: a read_buffer per level is somewhat expensive
  // we could just read back from disk instead (more IOs though)
  char ***flush_buffers;
  char ***flush_positions; // pointers into the flush_buffers

  /*
   * root node and functions for handling it
   */
  char *root_node;
  flush_ret_t flush_root();
  flush_ret_t flush_control_block(BufferControlBlock *bcb, bool force=false);
  flush_ret_t flush_internal_node(BufferControlBlock *bcb);
  flush_ret_t flush_leaf_node(BufferControlBlock *bcb, bool force);
  uint root_position;
  std::mutex root_lock;

  /*
   * function which actually carries out the flush. Designed to be
   * called either upon the root or upon a buffer at any level of the tree
   * @param data        the data to flush
   * @param size        the size of the data in bytes
   * @param begin       the smallest id of the node's children
   * @param min_key     the smalleset key this node is responsible for
   * @param max_key     the largest key this node is responsible for
   * @param options     the number of children this node has
   * @param level       the level of the buffer being flushed (0 is root)
   * @returns nothing
   */
  flush_ret_t do_flush(char *data, uint32_t size, uint32_t begin, 
    Node min_key, Node max_key, uint16_t options, uint8_t level);

  // Circular queue in which we place leaves that fill up
  CircularQueue *cq;

public:
  /**
   * Generates a new homebrew buffer tree.
   * @param dir     file path of the data structure root directory, relative to
   *                the executing workspace.
   * @param size    the minimum length of one buffer, in updates. Should be
   *                larger than the branching factor. not guaranteed to be
   *                the true buffer size, which can be between size and 2*size.
   * @param b       branching factor.
   * @param nodes   number of nodes in the graph
   * @param workers the number of workers which will be using this buffer tree (defaults to 1)
   * @param queue_factor  the factor we multiply by workers to get number of queue slots
   */
  BufferTree(std::string dir, uint32_t size, uint32_t b, Node nodes, int workers, int queue_factor);
  ~BufferTree();
  /**
   * Puts an update into the data structure.
   * @param upd the edge update.
   * @return nothing.
   */
  insert_ret_t insert(update_t upd);

  /*
   * Ask the buffer tree for data and sleep if necessary until it is available.
   * @param data       this is where to the key and vector of updates associated with it
   * @return           true true if got valid data, false if unable to get data.
   */
  bool get_data(data_ret_t &data);

  /**
   * Flushes the entire tree down to the leaves.
   * @return nothing.
   */
  flush_ret_t force_flush();

  /*
   * Notifies all threads waiting on condition variables that 
   * they should check their wait condition again
   * Useful when switching from blocking to non-blocking calls
   * to the circular queue
   * For example: we set this to true when shutting down the graph_workers
   * @param    block is true if we should turn on non-blocking operations
   *           and false if we should turn them off
   * @return   nothing
   */
  void set_non_block(bool block);

  /*
   * Function to convert an update_t to a char array
   * @param   dst the memory location to put the serialized data
   * @param   src the edge update
   * @return  nothing
   */
  void serialize_update(char *dst, update_t src);

  /*
   * Function to covert char array to update_t
   * @param src the memory location to load serialized data from
   * @param dst the edge update to put stuff into
   * @return nothing
   */
  update_t deserialize_update(char *src);
 
  /*
   * Copy the serialized data from one location to another
   * @param src data to copy from
   * @param dst data to copy to
   * @return nothing
   */
  static void copy_serial(char *src, char *dst);

  /*
   * Load a key from serialized data
   * @param location data to pull from
   * @return the key pulled from the data
   */
  static Node load_key(char *location);

  /*
   * Creates the entire buffer tree to produce a tree of depth log_B(N)
   */
  void setup_tree();

  /*
   * Static variables which track universal information about the buffer tree which
   * we would like to be accesible to all the bufferControlBlocks
   */
  static uint page_size;
  static const uint serial_update_size = sizeof(Node) + sizeof(Node);
  static uint8_t max_level;
  static uint32_t buffer_size;
  static uint64_t backing_EOF;
  static uint64_t leaf_size;
  /*
   * File descriptor of backing file for storage
   * and a mapping to it using mmap
   */
  int backing_store;
  static char *mapped_store;
};

class BufferFullError : public std::exception {
private:
  int id;
public:
  BufferFullError(int id) : id(id) {};
  virtual const char* what() const throw() {
    if (id == -1)
      return "Root buffer is full";
    else
      return "Non-Root buffer is full";
  }
};

class KeyIncorrectError : public std::exception {
public:
  virtual const char * what() const throw() {
    return "The key was not correct for the associated buffer";
  }
};


#endif //FASTBUFFERTREE_BUFFER_TREE_H
