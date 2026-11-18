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
	mqd_t tlm_queue;
	if ((tlm_queue = mq_open(TLM_QUEUE, O_WRONLY)) == (mqd_t)-1) {
		perror("altimeter: mq_open");
		pthread_exit(NULL);
	}
	struct tlm_packet packet;		

	// Handle the periodic behavior of the task
	start_periodic_timer(th, ALTIMETER_OFFSET);
	
	while (1) {

		// Handle the next activation of the task
		wait_next_activation(th);
		// 1. TODO: Copy the last altimeter reading
		pthread_mutex_lock(&altimeter_memory->lock);
		reading = *(altimeter_memory->ready);
		pthread_mutex_unlock(&altimeter_memory->lock);
		// 2. Simulate computation of the altimeter reading
		compute(th->wcet);

		// 3a. TODO: Send the reading as telemetry 
		// SUGGESTION: use the tlm_pack_altimeter function (telemetry.h) to pack the telemetry message
		tlm_pack_altimeter(&packet, &reading);
		// 3b. TODO: send the telemetry packet over the telemetry queue
		if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
			perror("Error sending telemetry packet");
			break;
		}
		// 4. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
		response_us = response_time_us(th);
		if (response_us > th->deadline) {
			tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
				perror("Altimeter: Error sending telemetry packet");
				break;
			}
		}
	}
}
