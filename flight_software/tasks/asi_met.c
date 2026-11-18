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
	struct tlm_packet packet;	

	// TODO: handle the periodic behavior of the task

	while (1) {

		// TODO: handle the next activation of the task

		// 1. TODO: Read all the messages waiting in the pipe and keep a running sum of temperature and pressure
		// SUGGESTION: look at the rtos/services/pipe/pipe.h for the pipe_read function
		num_readings = 0;
		sum_temperature = 0;
		sum_pressure = 0;

		// 2a. TODO: Compute the mean of the sensor readings and send them as telemetry, only if at least one reading was received
		// SUGGESTION: use the tlm_pack_asi_met function (telemetry.h) to pack the telemetry packet

		// 2b. TODO: send the telemetry packet over the telemetry queue

		// 3. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
	}
}
