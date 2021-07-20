#include "../include/buffer_flusher.h"
#include "../include/buffer_tree.h"

bool BufferFlusher::shutdown = false;
std::condition_variable BufferFlusher::flush_ready;
std::queue<buffer_id_t> BufferFlusher::flush_queue;
std::mutex BufferFlusher::queue_lock;

BufferFlusher::BufferFlusher(uint32_t id, BufferTree *bt) 
 : id(id), bt(bt) {
 	shutdown = false;
 	pthread_create(&thr, NULL, BufferFlusher::start_flusher, this);
}

BufferFlusher::~BufferFlusher() {
	shutdown = true;
	flush_ready.notify_all();
	pthread_join(thr, NULL);
}

void BufferFlusher::do_work() {
	while(true) {
		std::unique_lock<std::mutex> queue_unique(queue_lock);
		flush_ready.wait(queue_unique, [this]{return (!flush_queue.empty() || shutdown);});
		// printf("BufferFlusher id=%i awoken... ", id);
		if (!flush_queue.empty()) {
			buffer_id_t bcb_id = flush_queue.front();
			flush_queue.pop();
			// printf("Processing buffer %u\n", bcb_id);
			queue_unique.unlock();
			if (bcb_id >= bt->buffers.size()) {
				fprintf(stderr, "ERROR: the id given in the flush_queue is too large! %u\n", bcb_id);
				exit(EXIT_FAILURE);
			}

			BufferControlBlock *bcb = bt->buffers[bcb_id];
			bcb->lock();
			bt->flush_control_block(bcb);
			bcb->unlock();
			BufferControlBlock::buffer_ready.notify_one();
		} else if (shutdown) {
			// printf("BufferFlusher %i shutting down\n", id);
			queue_unique.unlock();
			return;
		} else {
			// printf("spurious wake-up\n");
			queue_unique.unlock(); // spurious wake-up. Go back to sleep
		}
	}
}
