#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>
#include <time.h>

struct periodic_thread {
	int index;					
	struct timespec r;		
	struct timespec release;	
	int period;				
	int wcet;					
	int deadline;			
	int priority;
};

void start_periodic_timer(struct periodic_thread * thd, uint64_t offs);
void wait_next_activation(struct periodic_thread * thd);	

long int response_time_us(struct periodic_thread * thd);
long int elapsed_ms(void);				

#endif
