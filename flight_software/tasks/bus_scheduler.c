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
	mqd_t tlm_queue;
	if (tlm_queue = mq_open(TLM_QUEUE, O_WRONLY) == (mqd_t)-1) {
		perror("bus_scheduler: mq_open");
		pthread_exit(NULL);
	}
	
	struct tlm_packet packet;	

	// TODO: handle the periodic behavior of the task
	start_periodic_timer(th, BUS_SCHEDULER_OFFSET);

	while (1) {

		// TODO: handle the next activation of the task
		wait_next_activation(th);
		// 1. TODO: Check the distribution done flag and clear it
		pthread_mutex_lock(&cycle_check->lock);
		distribution_done = &cycle_check->distribution_done;
		cycle_check->distribution_done = false;
		pthread_mutex_unlock(&cycle_check->lock);
		// 2. TODO: If the distribution flag was not set, reset the system
		// SUGGESTION: look at the rtos/system/system.h file for the system_reset function
		if (!distribution_done){
			system_reset(cycle_cnt);
		}
		// 3. Simulate computation
		compute(th->wcet);

		// 4. TODO: Prepare the transactions of the next cycle
		// SUGGESTION: use the bus_1553_prepare function (rtos/driver/bus_1553/bus_1553.h) to prepare the next cycle transactions
		bus_1553_prepare(&next);
		// 5. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
		response_us = response_time_us(th);
		if (response_us > th->deadline) {
			tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
				perror("Accelerometer: Error sending telemetry packet");
				break;
			}
		}
		cycle_cnt++;
	}
}
