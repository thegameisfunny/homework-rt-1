#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include "timing.h"		
#include "workload.h"		
#include "telemetry.h"		
#include "tasks.h"			

void* accelerometers_task(void* arg){

	struct accelerometers_str *accelerometers = (struct accelerometers_str *) arg;
	struct periodic_thread *th = &accelerometers->th;

	struct accelerometers_memory_str *accelerometers_memory = accelerometers->accelerometers_memory;
	struct accelerometers_reading reading;		
	long int response_us;	

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;		

	// TODO: handle the periodic behavior of the task

	while (1) {

		// TODO: handle the next activation of the task 

		// 1. TODO: Copy the last accelerometers reading

		// 2. Simulate computation of the accelerometers reading
		compute(th->wcet);

		// 3a. TODO: Send the reading as telemetry 
		// SUGGESTION: use the tlm_pack_accelerometers function (telemetry.h) to pack the telemetry message
		
		// 3b. TODO: send the telemetry packet over the telemetry queue

		// 4. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet

	}
}
