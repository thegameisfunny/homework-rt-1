#ifndef TASKS_H
#define TASKS_H

#include "timing.h"					
#include "schedule.h"
#include "shared_resources.h"	


/* These are the data structures each tasks uses for its state and shared resources. 
 * They are passed as arguments to the corresponding task functions at creation time.
*/

struct bus_scheduler_str {
	struct periodic_thread th;
	struct cycle_check_str *cycle_check;						
};

struct distribution_str {
	struct periodic_thread th;
	struct accelerometers_memory_str *accelerometers_memory;	
	struct altimeter_memory_str *altimeter_memory;				
	struct cycle_check_str *cycle_check;						
};

struct accelerometers_str {
	struct periodic_thread th;
	struct accelerometers_memory_str *accelerometers_memory;	
};

struct altimeter_str {
	struct periodic_thread th;
	struct altimeter_memory_str *altimeter_memory;				
};

struct comms_str {
	struct periodic_thread th;									
};

struct asi_met_str {
	struct periodic_thread th;									
};

struct imp_str {
	unsigned int images;	// Image counter
};

void* bus_scheduler_task(void* arg);		
void* distribution_task(void* arg);		
void* accelerometers_task(void* arg);		
void* altimeter_task(void* arg);			
void* comms_task(void* arg);				
void* asi_met_task(void* arg);			
void* imp_task(void* arg);				

#endif
