#define _GNU_SOURCE			
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mman.h>
#include "system.h"
#include "telemetry.h"	
#include "tasks/tasks.h"	


/***************************** Shared Data Initialization ********************************/

static struct accelerometers_memory_str accelerometers_memory = {
	.buffer  = { { .valid = false }, { .valid = false } },
	.filling = &accelerometers_memory.buffer[0],
	.ready   = &accelerometers_memory.buffer[1]
	// .lock: initialized by main
};

static struct altimeter_memory_str altimeter_memory = {
	.buffer  = { { .valid = false }, { .valid = false } },
	.filling = &altimeter_memory.buffer[0],
	.ready   = &altimeter_memory.buffer[1]
	// .lock: initialized by main
};

static struct cycle_check_str cycle_check = {
	.distribution_done = false
	// .lock: initialized by main
};


/***************************** Task Data ********************************/

static struct bus_scheduler_str bus_scheduler;
static struct distribution_str distribution;
static struct accelerometers_str accelerometers;
static struct altimeter_str altimeter;
static struct comms_str comms;
static struct asi_met_str asi_met;
static struct imp_str imp;

int create_thread_rt(pthread_t *thread, pthread_attr_t *myattr, void *(*task)(void *), void *arg, int priority)
{
	struct sched_param myparam;
	cpu_set_t rt_cpu;
	int error;

	myparam.sched_priority = priority;
	pthread_attr_setschedparam(myattr, &myparam);

	CPU_ZERO(&rt_cpu);
	CPU_SET(RT_CPU, &rt_cpu);
	error = pthread_create(thread, myattr, task, arg); //ritorna 0 se va tutto bene
	if(!error)
		pthread_setaffinity_np(*thread, sizeof(rt_cpu), &rt_cpu);
	return error;
}


int main(void){

	// a. Bus scheduler
	bus_scheduler.th.index    = BUS_SCHEDULER_ID;
	bus_scheduler.th.period   = BUS_SCHEDULER_PERIOD;
	bus_scheduler.th.wcet     = BUS_SCHEDULER_WCET;
	bus_scheduler.th.deadline = BUS_SCHEDULER_DEADLINE;
	bus_scheduler.th.priority = BUS_SCHEDULER_PRIORITY;
	bus_scheduler.cycle_check = &cycle_check;

	// b. Distribution
	distribution.th.index              = DISTRIBUTION_ID;
	distribution.th.period             = DISTRIBUTION_PERIOD;
	distribution.th.wcet               = DISTRIBUTION_WCET;
	distribution.th.deadline           = DISTRIBUTION_DEADLINE;
	distribution.th.priority           = DISTRIBUTION_PRIORITY;
	distribution.accelerometers_memory = &accelerometers_memory;
	distribution.altimeter_memory      = &altimeter_memory;
	distribution.cycle_check           = &cycle_check;

	// c. Accelerometers
	accelerometers.th.index              = ACCELEROMETERS_ID;
	accelerometers.th.period             = ACCELEROMETERS_PERIOD;
	accelerometers.th.wcet               = ACCELEROMETERS_WCET;
	accelerometers.th.deadline           = ACCELEROMETERS_DEADLINE;
	accelerometers.th.priority           = ACCELEROMETERS_PRIORITY;
	accelerometers.accelerometers_memory = &accelerometers_memory;

	// d. Altimeter
	altimeter.th.index         = ALTIMETER_ID;
	altimeter.th.period        = ALTIMETER_PERIOD;
	altimeter.th.wcet          = ALTIMETER_WCET;
	altimeter.th.deadline      = ALTIMETER_DEADLINE;
	altimeter.th.priority      = ALTIMETER_PRIORITY;
	altimeter.altimeter_memory = &altimeter_memory;

	// e. Comms: nothing shared
	comms.th.index    = COMMS_ID;
	comms.th.period   = COMMS_PERIOD;
	comms.th.wcet     = COMMS_WCET;
	comms.th.deadline = COMMS_DEADLINE;
	comms.th.priority = COMMS_PRIORITY;

	// f. ASI/MET: nothing shared, only the pipe
	asi_met.th.index    = ASI_MET_ID;
	asi_met.th.period   = ASI_MET_PERIOD;
	asi_met.th.wcet     = ASI_MET_WCET;
	asi_met.th.deadline = ASI_MET_DEADLINE;
	asi_met.th.priority = ASI_MET_PRIORITY;

	// g. IMP: not real-time, va implementato con polling server (task periodico che serve la richiesta)
	imp.th.index    = IMP_SERVER_ID;
	imp.th.period   = IMP_SERVER_PERIOD;
	imp.th.wcet     = IMP_SERVER_CAPACITY;
	imp.th.deadline = IMP_SERVER_DEADLINE;
	imp.th.priority = IMP_SERVER_PRIORITY;	//lo trattiamo come un thread periodico, quindi ha una priorità di deadline monotonic che andiamo a implementare in schedule.h
	imp.images = 0;

	// 2. TODO: Lock all current and future memory allocations of the program in RAM
	mlockall(MCL_CURRENT | MCL_FUTURE);//i flag MCL_CURRENT e MCL_FUTURE sono stati visti dalla funzione del manuale man 2 mlockall

	// 3. Start the system
	if (system_start() != 0) {
		fprintf(stderr, "Cannot start the system: run with sudo\n");
		return 1;
	}

	// 4. Create the telemetry queue
	struct mq_attr tlm_attr;

	tlm_attr.mq_flags = 0;
	tlm_attr.mq_maxmsg = TLM_MAX;
	tlm_attr.mq_msgsize = sizeof(struct tlm_packet);
	tlm_attr.mq_curmsgs = 0;

	mq_unlink(TLM_QUEUE);//svuota la coda di messaggi se esiste già, in modo da poterla ricreare

	mqd_t tlm_queue = mq_open(TLM_QUEUE, O_RDWR|O_CREAT|O_NONBLOCK, 0660, &tlm_attr);
	if (tlm_queue == (mqd_t)-1) {
		fprintf(stderr, "mq_open (telemetry)\n");
		return 1;
	}
	
	// 5. TODO: Create the tasks objects (pthread_t)
	pthread_t accelerometers_thread;
	pthread_t altimeter_thread;
	pthread_t asi_met_thread;
	pthread_t bus_scheduler_thread;
	pthread_t comms_thread;
	pthread_t distribution_thread;
	pthread_t imp_thread;

	// 6. TODO: Initialize the mutexes (shared_resources.h locks)
	pthread_mutexattr_t mutexattr;
	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&accelerometers_memory.lock, &mutexattr);
	pthread_mutex_init(&altimeter_memory.lock, &mutexattr);
	pthread_mutex_init(&cycle_check.lock, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	// 7. TODO: Configure and initialize the real-time threads (CPU pinning included)
	pthread_attr_t myattr;
	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);
	//accelerometers
	if(create_thread_rt(&accelerometers_thread, &myattr, accelerometers_task, &accelerometers, accelerometers.th.priority) != 0) {
		fprintf(stderr, "Failed to create accelerometers thread\n");
		return 1;
	}
	//altimeter
	if(create_thread_rt(&altimeter_thread, &myattr, altimeter_task, &altimeter, altimeter.th.priority) != 0) {
		fprintf(stderr, "Failed to create altimeter thread\n");
		return 1;
	}
	//asi_met
	if(create_thread_rt(&asi_met_thread, &myattr, asi_met_task, &asi_met, asi_met.th.priority) != 0) {
		fprintf(stderr, "Failed to create asi_met thread\n");
		return 1;
	}
	//bus_scheduler
	if(create_thread_rt(&bus_scheduler_thread, &myattr, bus_scheduler_task, &bus_scheduler, bus_scheduler.th.priority) != 0) {
		fprintf(stderr, "Failed to create bus_scheduler thread\n");
		return 1;
	}
	//comms
	if(create_thread_rt(&comms_thread, &myattr, comms_task, &comms, comms.th.priority) != 0) {
		fprintf(stderr, "Failed to create comms thread\n");
		return 1;
	}
	//distribution
	if(create_thread_rt(&distribution_thread, &myattr, distribution_task, &distribution, distribution.th.priority) != 0) {
		fprintf(stderr, "Failed to create distribution thread\n");
		return 1;
	}
	pthread_attr_destroy(&myattr);

	// 8. TODO: Configure and create the OPTIONAL IMP task (not real-time)
	if(create_thread_rt(&imp_thread, &myattr, imp_task, &imp, imp.th.priority) != 0) {
		fprintf(stderr, "Failed to create imp server thread\n");
		return 1;
	}

	// 9. Wait for the user to quit the program
	printf("Mars Pathfinder. Ground station: run  make ground  in another terminal\n\n");

	int c;
	do {
		c = getchar();
	} while (c != 'q' && c != EOF);

	// 10. TODO: Clean up the threads
	pthread_cancel(accelerometers_thread);
	pthread_cancel(altimeter_thread);
	pthread_cancel(asi_met_thread);
	pthread_cancel(bus_scheduler_thread);
	pthread_cancel(comms_thread);
	pthread_cancel(distribution_thread);

	pthread_cancel(imp_thread);

	pthread_join(accelerometers_thread, NULL);
	pthread_join(altimeter_thread, NULL);
	pthread_join(asi_met_thread, NULL);
	pthread_join(bus_scheduler_thread, NULL);
	pthread_join(comms_thread, NULL);
	pthread_join(distribution_thread, NULL);

	pthread_join(imp_thread, NULL);

	// 11. Stop the system
	system_stop();

	// 12. TODO: Clean up the telemetry queue
	mq_close(tlm_queue);
	mq_unlink(TLM_QUEUE);

	// 13. TODO: Destroy the mutexes
	pthread_mutex_destroy(&accelerometers_memory.lock);
	pthread_mutex_destroy(&altimeter_memory.lock);
	pthread_mutex_destroy(&cycle_check.lock);

	munlockall();

	return 0;
}
