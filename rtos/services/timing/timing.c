#include <stdint.h>
#include <time.h>
#include "system.h"		
#include "timing.h"

#define NSEC_PER_SEC   1000000000L
#define NSEC_PER_MSEC  1000000L

static void timespec_add_us(struct timespec *t, uint64_t d)
{
    d *= 1000;
    t->tv_nsec += d;
    t->tv_sec += t->tv_nsec / NSEC_PER_SEC;
    t->tv_nsec %= NSEC_PER_SEC;
}

static long int timespec_difference_ns(struct timespec *ts1, struct timespec *ts2){
	long int diff_sec, diff_nsec;
	long int difference_ns;
	diff_sec =(ts1->tv_sec - ts2->tv_sec);
	diff_nsec = (ts1->tv_nsec - ts2->tv_nsec);		
	difference_ns = diff_sec * NSEC_PER_SEC + diff_nsec;
	return difference_ns;
}

void start_periodic_timer(struct periodic_thread * thd, uint64_t offs)
{
    thd->r = system_epoch();
    timespec_add_us(&(thd->r), offs);
}

void wait_next_activation(struct periodic_thread * thd)
{
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &(thd->r), NULL);
    thd->release = thd->r;
    timespec_add_us(&(thd->r), thd->period);
}

long int response_time_us(struct periodic_thread * thd)
{
	struct timespec now;
	long int response_us;

	clock_gettime(CLOCK_REALTIME, &now);
	response_us = timespec_difference_ns(&now, &(thd->release)) / 1000;
	return response_us;
}

long int elapsed_ms(void)
{
	struct timespec now;
	struct timespec epoch = system_epoch();
	long int t_ms;

	clock_gettime(CLOCK_REALTIME, &now);
	t_ms = timespec_difference_ns(&now, &epoch) / NSEC_PER_MSEC;
	return t_ms;
}
