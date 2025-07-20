# Concurrency Memory Manager using Pthreads

__README Status: Draft__

## Overview
This program implements a from-scratch, custom memory manager using Pthreads and ANSI C. It provides a thread-safe memory allocation system tha uses segmentation and free list management to handle dynamic memory allocation requests. A test harness has been included to demonstrate the functionality of the memory manager. 

## Exposed Functions (my_malloc.h)
- my_malloc: Handles a memory allocation request. Upon first call, initializes memory region. 
- my_free: Frees a previously allocated memory block. Address must have been previously allocated by my_malloc.
- free_base_memory: Frees the base memory region allocated by my_malloc.

## Architecture
The memory manager splits the base memory region into 5 segments, 4 of which are used for smaller, more-frequent allocations. The remaining segment is set aside for larger allocations. Within each segment, the memory manager uses a free list to manage free memory blocks. Each block has a header that contains metadata about the block, including its size and whether it is free or allocated. The memory manager uses mutexes to ensure thread safety when accessing the free list.

## Test Harness
The "manager" executable contains a default test harness that demonstrates the functionality of the memory manager. It runs multiple threads and continuously allocates and frees memory blocks of various sizes. Metrics such as allocation time, free time, and memory usage are printed to the console. The test harness can be modified to test different scenarios or to stress-test the memory manager.
