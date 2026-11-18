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

	// g. IMP: not real-time
	imp.images = 0;

	// 2. TODO: Lock all current and future memory allocations of the program in RAM
	//TESTING

	// 3. Start the system
	if (system_start() != 0) {
		fprintf(stderr, "Cannot start the system: run with sudo\n");
		return 1;
	}

	// 4. TODO:Create the telemetry queue
	// SUGGESTION: look at shared_resources.h and telemetry.h for the configurations types and parameters

	// 5. TODO: Create the tasks objects (pthread_t)

	// 6. TODO: Initialize the mutexes (shared_resources.h locks)

	// 7. TODO: Configure and initialize the real-time threads (CPU pinning included)
	
	// 8. TODO: Configure and create the OPTIONAL IMP task (not real-time)

	// 9. Wait for the user to quit the program
	printf("Mars Pathfinder. Ground station: run  make ground  in another terminal\n\n");

	int c;
	do {
		c = getchar();
	} while (c != 'q' && c != EOF);

	// 10. TODO: Clean up the threads

	// 11. Stop the system
	system_stop();

	// 12. TODO: Clean up the telemetry queue

	// 13. TODO: Destroy the mutexes

	return 0;
}
