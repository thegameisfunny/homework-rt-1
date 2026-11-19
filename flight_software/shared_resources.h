#ifndef SHARED_RESOURCES_H
#define SHARED_RESOURCES_H

#include <pthread.h>
#include <stdbool.h>
#include "bus_1553.h"			

/* Double-buffered shared resources for the sensors: accelerometers and altimeter. 
 * Each has a buffer of two readings:
 * - A pointer to the one being filled
 * - A pointer to the one ready for readingss. 
 */

struct accelerometers_memory_str {
	struct accelerometers_reading buffer[2];
	struct accelerometers_reading *filling;	
	struct accelerometers_reading *ready;	
	pthread_mutex_t lock;
};

struct altimeter_memory_str {
	struct altimeter_reading buffer[2];
	struct altimeter_reading *filling;		
	struct altimeter_reading *ready;	
	pthread_mutex_t lock;				
};

/* Flag shared between the distribution task and the bus scheduler task.
 * Indicates whether the distribution for the current cycle is done.
 */

struct cycle_check_str {
	bool distribution_done;		
	pthread_mutex_t lock;
};

/* Shared queue for telemetry messages. */
#define TLM_QUEUE  "/tlm_queue"
#define TLM_MAX    64			

#define POLLING_QUEUE "/polling_queue"
#define ACTIVATION_QUEUE "/activation_queue"

#endif
