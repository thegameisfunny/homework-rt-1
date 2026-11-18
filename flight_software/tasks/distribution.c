#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include "timing.h"			
#include "workload.h"		
#include "bus_1553.h"		
#include "pipe.h"		
#include "telemetry.h"		
#include "tasks.h"

void* distribution_task(void* arg){

	struct distribution_str *distribution = (struct distribution_str *) arg;
	struct periodic_thread *th = &distribution->th;

	struct accelerometers_memory_str *accelerometers_memory = distribution->accelerometers_memory;
	struct altimeter_memory_str *altimeter_memory = distribution->altimeter_memory;
	struct cycle_check_str *cycle_check = distribution->cycle_check;

	struct bus_data data;			
	struct accelerometers_reading *completed_accelerometers;	
	struct altimeter_reading *completed_altimeter;				

	long int response_us;			

	// TODO: Open the bus transfer complete queue to receive the transfer complete event
	struct bus_transfer_complete_msg complete;

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;		

	while (1) {

		// 1. TODO: Handle the next activation of the task
		// SUGGESTION: Wait for the bus transfer complete event

		// 2. Save the release time of this activation
		th->release = complete.t1;

		// 3. TODO: Collect the bus data from the network card
		// SUGGESTION: use the bus_1553_collect function (rtos/driver/bus_1553/bus_1553.h) to collect the bus data

		// 4. Simulate work on the collected bus data
		compute(th->wcet);

		// 5a. TODO: Read the accelerometers data collected from the bus and store it in the accelerometers double buffer
		// SUGGESTION: Don't forget to swap the filling and ready buffers 

		// 5b. TODO: Read the altimeter data collected from the bus and store it in the altimeter double buffer
		// SUGGESTION: Don't forget to swap the filling and ready buffers 

		// 6. TODO: Send the ASI/MET data to the asi_met task through the pipe, only if the data is valid
		// SUGGESTION: use the pipe_write function (rtos/services/pipe/pipe.h) to send the ASI/MET data

		// 7. TODO: Assert the distribution done flag signaling to the bus scheduler that distribution is complete

		// 8. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet

		// 9. TODO: Report the response time of this activation as telemetry
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_timing function (telemetry.h) to pack the timing telemetry packet

	}
}
