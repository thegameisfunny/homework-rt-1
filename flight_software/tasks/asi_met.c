#include <fcntl.h>
#include <mqueue.h>
#include "timing.h"			
#include "pipe.h"			
#include "telemetry.h"		
#include "tasks.h"			

void* asi_met_task(void* arg){

	struct asi_met_str *asi_met = (struct asi_met_str *) arg;
	struct periodic_thread *th = &asi_met->th;
	
	struct asi_met_reading reading;
	struct asi_met_reading mean;
	int num_readings;
	long int sum_temperature;
	long int sum_pressure;
	long int response_us;

	// TODO: open the telemetry queue for sending telemetry packets
	mqd_t tlm_queue;
	if ((tlm_queue = mq_open(TLM_QUEUE, O_WRONLY)) == (mqd_t)-1) {
		perror("asi_met: mq_open");
		pthread_exit(NULL);
	}
	
	struct tlm_packet packet;	

	// TODO: handle the periodic behavior of the task
	start_periodic_timer(th, ASI_MET_OFFSET);
	while (1) {

		// TODO: handle the next activation of the task
		wait_next_activation(th);
		// 1. TODO: Read all the messages waiting in the pipe and keep a running sum of temperature and pressure
		// SUGGESTION: look at the rtos/services/pipe/pipe.h for the pipe_read function
		num_readings = 0;
		sum_temperature = 0;
		sum_pressure = 0;
		while (pipe_read(&reading, sizeof(reading))){ //pipe_read ritorna 0 o 1 se ha successo, dunque se la pipe è vuota da zero
			if(reading.valid){
				num_readings++;
				sum_temperature += reading.temperature_c;
				sum_pressure += reading.pressure_pa;
			}
		}

		// 2a. TODO: Compute the mean of the sensor readings and send them as telemetry, only if at least one reading was received
		// SUGGESTION: use the tlm_pack_asi_met function (telemetry.h) to pack the telemetry packet
		if (num_readings > 0){
			mean.temperature_c = sum_temperature / num_readings;
			mean.pressure_pa = sum_pressure / num_readings;
			tlm_pack_asi_met(&packet, &mean);
		// 2b. TODO: send the telemetry packet over the telemetry queue
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
				perror("asi_met: Error sending telemetry packet");
				break;
			}
		// 3. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
			response_us = response_time_us(th);
			if (response_us > th->deadline) {
				tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
				if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
					perror("Asi_met: Error sending telemetry packet");
					break;
				}
			}
		}
	}
}
