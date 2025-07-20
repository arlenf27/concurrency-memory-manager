#include <stddef.h>

/* NOTE: Underlying implementation of memory allocator is hidden in my_malloc.c for abstraction purposes. */

/* Total size (in bytes) of memory to be allocated */
#define TOTAL_SIZE 104857600

/* 
 * Allocates a block of memory of the specified size.
 * Upon first usage in program or on the first usage after each call to free_base_memory(), mallocs pre-allocated memory. 
 * Returns a pointer to the allocated memory or NULL if allocation fails.
 */
void* my_malloc(size_t size);

/* 
 * Frees a previously allocated block of memory.
 * Takes a pointer to the block to be freed.
 */
void my_free(void* ptr);

/* 
 * Frees pre-allocated memory.
 * This function should be called when the program is done using the memory.
 */
void free_base_memory();
