#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "my_malloc.h"

/* Test harness for my_malloc and my_free */

#define NUM_THREADS 16
#define OPS_PER_THREAD 100

#define SIXTEEN_B 16
#define ONE_KB 1024
#define FIFTY_KB 51200
#define ONE_HUNDRED_KB 102400

static pthread_mutex_t metrics_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long total_allocations = 0;
static unsigned long total_successes = 0;
static unsigned long total_frees = 0;
static unsigned long total_latency_ticks = 0;
static unsigned long large_attempts = 0;
static unsigned long large_successes = 0;
static unsigned long large_latency_ticks = 0;
static unsigned long large_latency_count = 0;

/* Randomly pick a size with the given distribution */
size_t choose_size(){
	double p = rand() / (double) RAND_MAX;
	if(p < 0.90){
		/* 90% small: 16 B – 1 KB */
		return SIXTEEN_B + (rand() % (ONE_KB - SIXTEEN_B + 1));
	}else if(p < 0.95){
		/*  5% mid: 1 KB – 50 KB */
		return ONE_KB + (rand() % (FIFTY_KB - ONE_KB + 1));
	}else{
		/*  5% large: 50 KB – 100 KB */
		return FIFTY_KB + (rand() % (ONE_HUNDRED_KB - FIFTY_KB + 1));
	}
	return 0;
}

/* Worker thread: perform OPS_PER_THREAD malloc/free cycles */
void* thread_worker(void* arg){
	int i;
	for(i = 0; i < OPS_PER_THREAD; i++){
		size_t sz = choose_size();
		/* Time the allocation via clock() */
		clock_t t0 = clock();
		void* ptr = my_malloc(sz);
		clock_t t1 = clock();
		/* Difference in clock ticks */
		clock_t dt = t1 - t0;
		/* Update statistics */
		pthread_mutex_lock(&metrics_mutex);
		total_allocations++;
		total_latency_ticks += (unsigned long)dt;
		if(sz >= ONE_KB){
			large_attempts++;
			if(ptr){
				large_successes++;
				large_latency_ticks += (unsigned long)dt;
				large_latency_count++;
			}
		}
		if(ptr) total_successes++;
		pthread_mutex_unlock(&metrics_mutex);
		/* Free if allocated */
		if(ptr){
			my_free(ptr);
			pthread_mutex_lock(&metrics_mutex);
			total_frees++;
			pthread_mutex_unlock(&metrics_mutex);
		}
	}
	return NULL;
}

int main(void){
	pthread_t threads[NUM_THREADS];
	clock_t start;
	clock_t end;
	double elapsed_s;
	unsigned long total_ops;
	double throughput;
	double avg_latency_us;
	double large_success_ratio;
	double avg_large_latency_us;
	int i;

	srand((unsigned)time(NULL));

	start = clock();

	/* Launch threads */
	for(i = 0; i < NUM_THREADS; i++){
		if(pthread_create(&threads[i], NULL, thread_worker, NULL) != 0){
			fprintf(stderr, "Error: pthread_create failed\n");
			return 1;
		}
	}
	/* Join threads */
	for(i = 0; i < NUM_THREADS; i++){
		pthread_join(threads[i], NULL);
	}
	
	end = clock();
	elapsed_s = (double) (end - start) / CLOCKS_PER_SEC;

	total_ops = total_allocations + total_frees;
	throughput = total_ops / elapsed_s;
	avg_latency_us = (double) total_latency_ticks / total_allocations * (1e6 / CLOCKS_PER_SEC);
	large_success_ratio = large_attempts ? (double) large_successes / large_attempts * 100.0 : 0.0;
	avg_large_latency_us = large_latency_count ? (double) large_latency_ticks / large_latency_count * (1e6 / CLOCKS_PER_SEC) : 0.0;

	/* Print results */
	printf("=== Test Harness Results ===\n");
	printf("Threads: %d\n", NUM_THREADS);
	printf("Ops per thread: %d\n", OPS_PER_THREAD);
	printf("Elapsed CPU time: %.3f s\n", elapsed_s);
	printf("Total ops (alloc+free): %lu\n", total_ops);
	printf("Throughput: %.1f ops/s\n", throughput);
	printf("Avg malloc latency: %.3f µs\n", avg_latency_us);
	printf("Total mallocs: %lu\n", total_allocations);
	printf("Total malloc successes: %lu\n", total_successes);
	printf("Success Ratio: %.2f%%\n", (double) total_successes / total_allocations * 100.0);
	printf("Large alloc attempts: %lu\n", large_attempts);
	printf("Large success ratio: %.2f%%\n", large_success_ratio);
	printf("Avg large latency: %.3f µs\n", avg_large_latency_us);

	/* Free pre-allocated memory */
	free_base_memory();

	return 0;
}
