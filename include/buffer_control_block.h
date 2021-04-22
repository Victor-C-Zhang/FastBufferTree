//
// Created by victor on 3/2/21.
//

#ifndef FASTBUFFERTREE_BUFFER_CONTROL_BLOCK_H
#define FASTBUFFERTREE_BUFFER_CONTROL_BLOCK_H


#include <cstdint>
#include <string>
#include <mutex>
#include <condition_variable>
#include "update.h"

typedef uint32_t buffer_id_t;
typedef uint32_t File_Pointer;
typedef std::pair<Node, buffer_id_t> work_t;

/**
 * Buffer metadata class. Care should be taken to synchronize access to the
 * *entire* data structure.
 */
class BufferControlBlock {
private:
  buffer_id_t id;
  // busy lock
  std::mutex mtx;

  // condition variable to determine if the buffer needs to be flushed
  // std::condition_variable needs_flushing;

  // how many items are currently in the buffer
  File_Pointer storage_ptr;

  // where in the file is our data stored
  File_Pointer file_offset;

  /*
   * Check if this buffer needs a flush
   * @return true if buffer needs a flush and it didn't need it before the most recent write
   */
  bool needs_flush(uint32_t size);

public:
  // this node's level in the tree. 0 is root, 1 is it's children, etc
  uint8_t level;

  // the index in the buffers array of this buffer's smallest child
  buffer_id_t first_child = 0;
  uint8_t children_num = 0;     // and the number of children

  // information about what keys this node will store
  Node min_key;
  Node max_key;

  /**
   * Generates metadata and file handle for a new buffer.
   * @param id an integer identifier for the buffer.
   * @param off the offset into the file at which this buffer's data begins
   * @param level the level in the tree this buffer resides at
   */
  BufferControlBlock(buffer_id_t id, uint32_t off, uint8_t level);

  /**
   * Lock the buffer for data transfer. Blocks the calling context if the
   * lock is unavailable.
   */
  inline void lock() {
    mtx.lock();
  }

  /**
   * Unlocks the buffer once data transfer is complete. Throws an error if
   * the buffer isn't locked.
   * @throw BufferNotLockedError if the buffer isn't locked.
   */
  inline void unlock() {
    mtx.unlock();
  }

  /**
   * Acquire this buffer's lock and return true iff the buffer is not busy
   * @return true if the caller now has the lock. False if the buffer was already locked.
   */
  inline bool try_lock() {
    return mtx.try_lock();
  }

  /*
   * Write to the buffer managed by this metadata.
   * @param data the data to write
   * @param size the size of the data written
   * @return true if buffer needs flush for the first time and false otherwise
   */
  bool write(char * data, uint32_t size);

  /*
   * Flush the buffer this block controls
   * @return nothing
   */
  void flush();

  inline void reset() {storage_ptr = 0;}
  inline buffer_id_t get_id() {return id;}
  inline work_t work_info() {return work_t(min_key, id);}
  inline File_Pointer size() {return storage_ptr;}
  inline File_Pointer offset() {return file_offset;}
  inline bool is_leaf() {return min_key == max_key;}

  inline void add_child(buffer_id_t child) {
    children_num++;
    first_child = (first_child == 0)? child : first_child;
  }

  inline void print() {
    printf("buffer %u: storage_ptr = %u, offset = %u, min_key=%u, max_key=%u, first_child=%u, #children=%u\n", 
      id, storage_ptr, file_offset, min_key, max_key, first_child, children_num);
  }
};

class BufferNotLockedError : public std::exception {
private:
  buffer_id_t id;
public:
  explicit BufferNotLockedError(buffer_id_t id) : id(id){}
  const char* what() const noexcept override {
    std::string temp = "Buffer not locked! id: " + std::to_string(id);
    // ok, since this will terminate the program anyways
    return temp.c_str();
  }
};

#endif //FASTBUFFERTREE_BUFFER_CONTROL_BLOCK_H
