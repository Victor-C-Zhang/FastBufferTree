//
// Created by victor on 3/2/21.
//

#include "../include/buffer_control_block.h"
#include "../include/buffer_tree.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>

BufferControlBlock::BufferControlBlock(buffer_id_t id, File_Pointer off, uint8_t level)
  : id(id), file_offset(off), level(level){
  storage_ptr = 0;
}

inline bool BufferControlBlock::needs_flush(uint32_t size) {
	if(is_leaf())
		return ((storage_ptr % BufferTree::leaf_size) < size) || (storage_ptr >= BufferTree::buffer_size);
	else
		return storage_ptr >= BufferTree::buffer_size;
}

bool BufferControlBlock::write(char *data, uint32_t size) {
	// printf("Writing to buffer %d data pointer = %p with size %i\n", id, data, size);
	if(storage_ptr + size > BufferTree::buffer_size + BufferTree::page_size) {
		printf("buffer %i too full write size %u\n", id, size);
		throw BufferFullError(id);
	}

	memcpy(BufferTree::mapped_store + file_offset + storage_ptr, data, size);
	storage_ptr += size;

	// return if this buffer should be added to the flush queue
	return needs_flush(size);
}
