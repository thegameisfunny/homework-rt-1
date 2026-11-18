#include <fcntl.h>
#include <mqueue.h>
#include "timing.h"			
#include "workload.h"		
#include "telemetry.h"
#include "radio.h"		
#include "tasks.h"		

void* comms_task(void* arg){

	struct comms_str *comms = (struct comms_str *) arg;
	struct periodic_thread *th = &comms->th;

	long int response_us;	

	// TODO: Open the TX ready queue to receive tx-ready event
	struct radio_tx_ready_msg tx_ready;	

	// TODO: Open the downlink queue for sending frames to the radio
	struct downlink_frame frame;

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;
	ssize_t received;

	while (1) {

		// 1. TODO: handle the next activation of the taskk
		// SUGGESTION: Wait for the radio transmitter (i.e., the tx-ready event)

		// 2. Save the release time of this activation
		th->release = tx_ready.time;

		// 3. TODO: Start a new, empty frame, for this transmission slot
		// SUGGESTION: use the frame_start function (rtos/drivers/radio/radio.h) to start a new frame

		// 4. TODO: Move the telemetry packets waiting in the queue into the downlink frame, until the queue is empty
		// SUGGESTION: Use mq_receive to get telemetry packets from the telemetry queue
		// SUGGESTION: Use frame_add(&frame, &packet, sizeof(packet)); to add the telemetry packet to the frame

		// 5. Simulate the computation of the frame encoding
		compute(th->wcet);

		// 6. TODO: Send the frame to the radio downlink queue

		// 7. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
	}
}
