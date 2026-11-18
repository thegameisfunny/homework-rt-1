#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdbool.h>
#include "timing.h"			
#include "workload.h"		
#include "bus_1553.h"		
#include "telemetry.h"		
#include "system.h"
#include "tasks.h"			

void* bus_scheduler_task(void* arg){

	struct bus_scheduler_str *bus_scheduler = (struct bus_scheduler_str *) arg;
	struct periodic_thread *th = &bus_scheduler->th;

	struct cycle_check_str *cycle_check = bus_scheduler->cycle_check;

	// Data structure containing the device schedule of the next bus transactions
	struct bus_transactions next = {.poll_accelerometers = true, .poll_altimeter = false, .poll_asi_met = true};

	unsigned int cycle_cnt = 0;			
	bool distribution_done;						
	long int response_us;			

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;	

	// TODO: handle the periodic behavior of the task

	while (1) {

		// TODO: handle the next activation of the task

		// 1. TODO: Check the distribution done flag and clear it

		// 2. TODO: If the distribution flag was not set, reset the system
		// SUGGESTION: look at the rtos/system/system.h file for the system_reset function

		// 3. Simulate computation
		compute(th->wcet);

		// 4. TODO: Prepare the transactions of the next cycle
		// SUGGESTION: use the bus_1553_prepare function (rtos/driver/bus_1553/bus_1553.h) to prepare the next cycle transactions

		// 5. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet

		cycle_cnt++;
	}
}
