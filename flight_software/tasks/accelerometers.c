#include <errno.h>
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
	mqd_t tlm_queue;
	if ((tlm_queue = mq_open(TLM_QUEUE, O_WRONLY | O_NONBLOCK)) == (mqd_t)-1) {
		perror("accelerometers: mq_open");
		pthread_exit(NULL);
	}
	//FINE TODO
	struct tlm_packet packet;		

	// TODO: handle the periodic behavior of the task
	start_periodic_timer(th, ACCELEROMETERS_OFFSET);
	//FINE TODO
	
	while (1) {

		// TODO: handle the next activation of the task
		wait_next_activation(th);

		// 1. TODO: Copy the last accelerometers reading
		pthread_mutex_lock(&accelerometers_memory->lock);
		reading = *(accelerometers_memory->ready);
		pthread_mutex_unlock(&accelerometers_memory->lock);
		
		// 2. Simulate computation of the accelerometers reading
		compute(th->wcet);

		// 3a. TODO: Send the reading as telemetry 
		// SUGGESTION: use the tlm_pack_accelerometers function (telemetry.h) to pack the telemetry message
		tlm_pack_accelerometers(&packet, &reading);
		
		// 3b. TODO: send the telemetry packet over the telemetry queue
		if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0) == -1 && errno != EAGAIN)
			perror("accelerometers: Error sending telemetry packet");	// queue full: the packet is dropped, the task goes on
		
		// 4. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
		response_us = response_time_us(th);
		if (response_us > th->deadline) {
			tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0) == -1 && errno != EAGAIN)
			perror("accelerometers: Error sending telemetry packet");	// queue full: the packet is dropped, the task goes on
		}
	}
}
