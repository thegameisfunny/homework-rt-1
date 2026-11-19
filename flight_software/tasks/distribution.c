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
	mqd_t bus_transfer_complete_qd;
	if ((bus_transfer_complete_qd = mq_open(BUS_TRANSFER_COMPLETE_QUEUE, O_RDONLY)) == (mqd_t)-1) {
		perror("distribution: mq_open (bus transfer)");
		pthread_exit(NULL);
	}
	struct bus_transfer_complete_msg complete;

	// TODO: Open the telemetry queue for sending telemetry packets
	mqd_t tlm_queue;
	if ((tlm_queue = mq_open(TLM_QUEUE, O_WRONLY)) == (mqd_t)-1) {
		perror("distribution: mq_open (telemetry)");
		pthread_exit(NULL);
	}
	struct tlm_packet packet;		

	while (1) {

		// 1. TODO: Handle the next activation of the task
		// SUGGESTION: Wait for the bus transfer complete event
		mq_receive(bus_transfer_complete_qd, (char *)&complete, sizeof(complete), 0);
		// 2. Save the release time of this activation
		th->release = complete.t1;

		// 3. TODO: Collect the bus data from the network card
		// SUGGESTION: use the bus_1553_collect function (rtos/driver/bus_1553/bus_1553.h) to collect the bus data
		bus_1553_collect(&data);
		
		// 4. Simulate work on the collected bus data
		compute(th->wcet);

		// 5a. TODO: Read the accelerometers data collected from the bus and store it in the accelerometers double buffer
		// SUGGESTION: Don't forget to swap the filling and ready buffers 
		*accelerometers_memory->filling = data.accelerometers;

		pthread_mutex_lock(&accelerometers_memory->lock);
		completed_accelerometers = accelerometers_memory->filling;
		accelerometers_memory->filling = accelerometers_memory->ready;
		accelerometers_memory->ready = completed_accelerometers;		
		pthread_mutex_unlock(&accelerometers_memory->lock);
		
		// 5b. TODO: Read the altimeter data collected from the bus and store it in the altimeter double buffer
		// SUGGESTION: Don't forget to swap the filling and ready buffers  
		*altimeter_memory->filling = data.altimeter;

		pthread_mutex_lock(&altimeter_memory->lock);
		completed_altimeter = altimeter_memory->filling;
		altimeter_memory->filling = altimeter_memory->ready;
		altimeter_memory->ready = completed_altimeter;		
		pthread_mutex_unlock(&altimeter_memory->lock);

		// 6. TODO: Send the ASI/MET data to the asi_met task through the pipe, only if the data is valid
		// SUGGESTION: use the pipe_write function (rtos/services/pipe/pipe.h) to send the ASI/MET data
		if (data.asi_met.valid){
			pipe_write(&data.asi_met, sizeof(data.asi_met));
		}
		// 7. TODO: Assert the distribution done flag signaling to the bus scheduler that distribution is complete
		pthread_mutex_lock(&cycle_check->lock);
		cycle_check->distribution_done = true;
		pthread_mutex_unlock(&cycle_check->lock);
		
		// 8. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
		response_us = response_time_us(th);
		if (response_us > th->deadline) {
			tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(packet), 0)==-1){
				perror("Distribution: Error sending deadline miss telemetry packet");
				break;
			}
		}
		// 9. TODO: Report the response time of this activation as telemetry
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_timing function (telemetry.h) to pack the timing telemetry packet
		tlm_pack_timing(&packet, response_us, th->deadline);
		if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
			perror("Distribution: Error sending timing telemetry packet");
			break;
		}
	}
}
