#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include "timing.h"			
#include "workload.h"		
#include "telemetry.h"		
#include "tasks.h"			

void* altimeter_task(void* arg){

	struct altimeter_str *altimeter = (struct altimeter_str *) arg;
	struct periodic_thread *th = &altimeter->th;

	struct altimeter_memory_str *altimeter_memory = altimeter->altimeter_memory;
	struct altimeter_reading reading;		
	long int response_us;	

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;		

	// Handle the periodic behavior of the task

	while (1) {

		// Handle the next activation of the task

		// 1. TODO: Copy the last altimeter reading

		// 2. Simulate computation of the altimeter reading
		compute(th->wcet);

		// 3a. TODO: Send the reading as telemetry 
		// SUGGESTION: use the tlm_pack_altimeter function (telemetry.h) to pack the telemetry message
		
		// 3b. TODO: send the telemetry packet over the telemetry queue

		// 4. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
	}
}
