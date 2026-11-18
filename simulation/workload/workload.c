#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "workload.h"

void compute(uint64_t wcet_us)
{
	struct timespec start, now;
	unsigned int seed;
	uint64_t max_us;				
	uint64_t us;					
	long int work_ns;				
	long int done_ns;				

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);

	seed = start.tv_nsec;
	max_us = wcet_us - wcet_us / 20;
	us = wcet_us / 2 + rand_r(&seed) % (max_us - wcet_us / 2 + 1);
	work_ns = us * 1000;

	do {
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
		done_ns = (now.tv_sec - start.tv_sec) * 1000000000L + (now.tv_nsec - start.tv_nsec);
	} while (done_ns < work_ns);
}
