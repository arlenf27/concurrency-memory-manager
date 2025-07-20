#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include "my_malloc.h"

/* Boolean definitions */
#define TRUE 1
#define FALSE 0
typedef unsigned char bool;

/* Number of segments */
#define NUM_SEGMENTS 5
#define SEG_1_TO_4_SIZE ((TOTAL_SIZE * (double) (20.0/100.0)) / (double) (NUM_SEGMENTS-1))
#define SEG_5_SIZE ((TOTAL_SIZE * (double) (80.0/100.0)))

/* Minimum split size in bytes */
#define MIN_SPLIT_SIZE 32

/* Maximum time in seconds to wait for large allocations */
#define MAX_WAIT_TIME 0.1
#define LARGE_SIZE 4194304

/* 
 * The following structure is used to manage memory blocks.
 * It contains the size of the block, pointers to the next and previous blocks,
 * and a flag indicating whether the block is free or not.
 */
typedef struct block_header{
	size_t size;
	struct block_header *next; 
	struct block_header *prev; 
	bool free; 
	int segment_id;
} block_header;

/* 
 * The following structure represents a memory segment.
 * It contains the size of the segment, a pointer to the start of the segment,
 * a pointer to the free list of blocks, and mutex locks/conditions for thread safety.
 */
typedef struct segment{
	size_t size; 
	void* start_ptr; 
	block_header* free_list;
	pthread_mutex_t lock; 
	pthread_cond_t condition; 
} segment;

/* Base memory pointer */
static void* base_ptr = NULL;

/* Array of segments */
static segment* segments = NULL;

/* 
 * Initializes the memory allocator of TOTAL_SIZE bytes. 
 * Allocates TOTAL_SIZE bytes of memory and sets up NUM_SEGMENTS segments.
 * Returns an array of segments or NULL if unsuccessful. 
 */
segment* initialize_allocator(){
	int i;
	char* allocation_iterator;
	segment* new_segments;
	base_ptr = malloc(TOTAL_SIZE);
	allocation_iterator = (char*) base_ptr;
	if(base_ptr == NULL) return NULL;
	new_segments = (segment*) malloc(sizeof(segment) * NUM_SEGMENTS);
	if(new_segments == NULL){
		return NULL;
	}
	for(i = 0; i < NUM_SEGMENTS; i++){
		size_t segment_size;
		segment_size = (i < NUM_SEGMENTS - 1) ? (SEG_1_TO_4_SIZE) : (SEG_5_SIZE);
		/* First segment starts at base_ptr address. */
		(new_segments+i)->start_ptr = (void*) (allocation_iterator);
		(new_segments+i)->size = segment_size;
		/* Free list is one large block initially that takes up the entire segment. */
		(new_segments+i)->free_list = (block_header*) ((new_segments+i)->start_ptr);
		(new_segments+i)->free_list->size = segment_size - sizeof(block_header);
		(new_segments+i)->free_list->next = NULL;
		(new_segments+i)->free_list->prev = NULL;
		(new_segments+i)->free_list->free = TRUE;
		(new_segments+i)->free_list->segment_id = i;
		/* Initialize mutex and condition variable for each segment */
		pthread_mutex_init(&((new_segments+i)->lock), NULL);
		pthread_cond_init(&((new_segments+i)->condition), NULL);
		allocation_iterator += segment_size;
	}
	return new_segments;
}

/* 
 * Adds a block to the free list.
 * free_list: Pointer to the head of the free list to add to. 
 * new_block: Pointer to the block to be added.
 * Returns the new head of the free list.
 */
block_header* add_to_free_list(block_header* free_list, block_header* new_block){
	assert(new_block != NULL);
	assert(new_block->free == TRUE);
	if(free_list == NULL){
		new_block->next = NULL;
		new_block->prev = NULL;
	}else{
		new_block->next = free_list;
		free_list->prev = new_block;
		new_block->prev = NULL;
	}
	return new_block;
}

/* 
 * Finds the smallest free block in the free list that is large enough to accommodate the requested size.
 * Returns NULL if no suitable block is found.
 */
block_header* find_best_fit(block_header* free_list, size_t size){
	block_header* best_fit;
	block_header* current;
	assert(size > 0);
	best_fit = NULL;
	current = free_list;
	while(current != NULL){
		if(current->free && current->size >= size){
			if(best_fit == NULL || current->size < best_fit->size){
				best_fit = current;
			}
		}
		current = current->next;
	}

	return best_fit;
}

/*
 * Splits a block into two smaller blocks if the remaining size is greater than or equal to MIN_SPLIT_SIZE.
 * The first block is marked as allocated and the second block is added to the free list.
 */
void split_block(segment* seg, block_header* block, size_t size){
	assert(block != NULL);
	assert(size > 0);
	if(block->size - size >= MIN_SPLIT_SIZE + sizeof(block_header)){
		block_header* new_block = (block_header*) ((char*) block + sizeof(block_header) + size);
		new_block->free = TRUE;
		new_block->size = block->size - size - sizeof(block_header);
		new_block->next = block->next;
		if(new_block->next != NULL) block->next->prev = new_block;
		new_block->prev = block;
		block->next = new_block;
		new_block->segment_id = block->segment_id;

		/* Shrink original block */
		block->size = size;
		block->free = FALSE;

		/* Unlink original block immediately */
		if(block->prev != NULL) block->prev->next = block->next;
		if(block->next != NULL) block->next->prev = block->prev;
		if(seg->free_list == block){
			seg->free_list = block->next;
		}
	}
}

/* 
 * Merges two adjacent free blocks into a single larger block.
 */
void merge_blocks(block_header* block1, block_header* block2){
	assert(block1 != NULL);
	assert(block2 != NULL);
	assert(block1->free && block2->free);
	assert((char*) block1 + sizeof(block_header) + block1->size == (char*) block2);
	if(block2->prev != NULL) block2->prev->next = block2->next;
	if(block2->next != NULL) block2->next->prev = block2->prev;
	block1->size += block2->size + sizeof(block_header);
	block1->next = block2->next;
	if(block2->next != NULL) block2->next->prev = block1;
}

/* 
 * Handles large allocations by waiting for a free block to become available.
 * Blocks the calling thread until a suitable block is found.
 * Returns a pointer to the free block or NULL if not found.
 */
block_header* wait_for_free_block(segment* seg, size_t size){
	struct timespec timeout;
	time_t start_time;
	block_header* block = NULL;

	pthread_mutex_lock(&seg->lock);
	assert(seg != NULL);
	assert(size > 0);
	assert(seg->free_list != NULL);
	start_time = time(NULL);
	timeout.tv_sec = start_time + (time_t) MAX_WAIT_TIME;
	/* Nanoseconds not used here */
	timeout.tv_nsec = 0;
	while(1){
		int rc;
		block = find_best_fit(seg->free_list, size);
		if(block != NULL || size > TOTAL_SIZE) break;
		/* Wait for a free block to become available with a timeout */
		rc = pthread_cond_timedwait(&seg->condition, &seg->lock, &timeout);
		if(rc == ETIMEDOUT) break;
	}

	if(block == NULL) pthread_mutex_unlock(&seg->lock);

	return block;
}

void* my_malloc(size_t size){
	static int current_segment = 0;
	static bool initialized = FALSE;
	static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_mutex_t round_robin_mutex = PTHREAD_MUTEX_INITIALIZER;
	block_header* block;
	void* ptr;
	int seg_id = 0;
	size_t actual_size = size + sizeof(block_header);
	int i;
	assert(size > 0);
	/* Ensure initialization only happens once by locking the mutex */
	pthread_mutex_lock(&init_mutex);
	if(!initialized){
		/* Initialize the allocator */
		segments = initialize_allocator();
		if(segments == NULL){
			pthread_mutex_unlock(&init_mutex);
			pthread_mutex_destroy(&init_mutex);
			pthread_mutex_destroy(&round_robin_mutex);
			free(segments);
			free(base_ptr);
			return NULL;
		}
		initialized = TRUE;
	}
	pthread_mutex_unlock(&init_mutex);
	/* Use round robin allocation for the small segments*/
	pthread_mutex_lock(&round_robin_mutex);
	seg_id = current_segment;
	current_segment = (current_segment + 1) % (NUM_SEGMENTS-1);
	pthread_mutex_unlock(&round_robin_mutex);

	pthread_mutex_lock(&((segments + seg_id)->lock));
	block = find_best_fit((segments+seg_id)->free_list, actual_size);
	if(block != NULL){
		/* If a suitable block is found, split it and return the pointer */
		split_block(segments + seg_id, block, size);
	}else{
		/* Release the current segment lock before checking all segments */
		pthread_mutex_unlock(&((segments + seg_id)->lock));
		/* If no suitable block is found, wait for a free block for each segment
		 * For large allocations, wait for the fifth segment for a free block*/
		if(size <= LARGE_SIZE){
			for(i = 0; i < NUM_SEGMENTS-1; i++){
				block = wait_for_free_block(segments + i, actual_size);
				if(block != NULL){
					seg_id = i;
					break;
				}
			}
		}else{
			block = wait_for_free_block(segments + NUM_SEGMENTS-1, actual_size);
			if(block != NULL){
				seg_id = NUM_SEGMENTS-1;
			}
		}
		if(block == NULL){
			return NULL;
		}
		block->segment_id = seg_id;
		/* After finding a free block, split it */
		split_block(segments + seg_id, block, size);
	}
	/* Mark the block as allocated */
	ptr = (void*) ((char*) block + sizeof(block_header));
	block->free = FALSE;
	pthread_mutex_unlock(&((segments + seg_id)->lock));
	/* Return the pointer to the allocated memory */
	return ptr;
}

void my_free(void* ptr){
	segment* seg;
	int seg_id;
	block_header* hdr;
	if (ptr == NULL) return;
    	hdr = (block_header*) ((char*) ptr - sizeof(block_header));
	/* Must be stored in the header */
    	seg_id = hdr->segment_id;  
	seg = segments + seg_id;
    	pthread_mutex_lock(&seg->lock);
    	hdr->free = TRUE;
    	/* Coalesce with prev */
    	if (hdr->prev && hdr->prev->free && (char*) hdr->prev + sizeof(block_header) + hdr->prev->size == (char*) hdr) {
        	merge_blocks(hdr->prev, hdr);
        	hdr = hdr->prev;
    	}
    	/* Coalesce with next */
    	if (hdr->next && hdr->next->free && (char*) hdr + sizeof(block_header) + hdr->size == (char*) hdr->next){
        	merge_blocks(hdr, hdr->next);
    	}

    	pthread_cond_broadcast(&seg->condition);
    	pthread_mutex_unlock(&seg->lock);
}

/* 
 * Frees base memory and destroys all segments, mutexes, and condition variables.
 */
void free_base_memory(){
	int i;
	for(i = 0; i < NUM_SEGMENTS; i++){
		pthread_mutex_destroy(&((segments + i)->lock));
		pthread_cond_destroy(&((segments + i)->condition));
	}
	free(segments);
	free(base_ptr);
}
