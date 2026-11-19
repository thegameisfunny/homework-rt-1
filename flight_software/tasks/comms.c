#include <errno.h>
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
	mqd_t tx_ready_qd;
	if ((tx_ready_qd = mq_open(RADIO_TX_READY_QUEUE, O_RDONLY)) == (mqd_t)-1) {
		perror("comms_task: mq_open (tx_ready)");
		pthread_exit(NULL);
		return NULL;
	}
	
	struct radio_tx_ready_msg tx_ready;	

	// TODO: Open the downlink queue for sending frames to the radio
	mqd_t downlink_qd;
	if ((downlink_qd = mq_open(RADIO_DOWNLINK_QUEUE, O_WRONLY)) == (mqd_t)-1) {
		perror("comms_task: mq_open (downlink)");
		pthread_exit(NULL);
		return NULL;
	}
	struct downlink_frame frame;

	// TODO: Open the telemetry queue for sending telemetry packets (IMHO: I think this should be opened also to receive)
	mqd_t tlm_queue;
	if ((tlm_queue = mq_open(TLM_QUEUE, O_RDWR|O_NONBLOCK)) == (mqd_t)-1) {
		perror("comms_task: mq_open");
		pthread_exit(NULL);
		return NULL;
	}
	struct tlm_packet packet;
	ssize_t received;

	while (1) {

		// 1. TODO: handle the next activation of the taskk
		// SUGGESTION: Wait for the radio transmitter (i.e., the tx-ready event)
		mq_receive(tx_ready_qd, (char *)&tx_ready, sizeof(tx_ready), 0);
		// 2. Save the release time of this activation
		th->release = tx_ready.time;

		// 3. TODO: Start a new, empty frame, for this transmission slot
		// SUGGESTION: use the frame_start function (rtos/drivers/radio/radio.h) to start a new frame
		frame_start(&frame, tx_ready.slot);
		// 4. TODO: Move the telemetry packets waiting in the queue into the downlink frame, until the queue is empty
		// SUGGESTION: Use mq_receive to get telemetry packets from the telemetry queue
		// SUGGESTION: Use frame_add(&frame, &packet, sizeof(packet)); to add the telemetry packet to the frame
		
		while((received = mq_receive(tlm_queue, (char *)&packet, sizeof(packet), 0))>0){
			frame_add(&frame, &packet, sizeof(packet));
		}
		// 5. Simulate the computation of the frame encoding
		compute(th->wcet);

		// 6. TODO: Send the frame to the radio downlink queue
		if (mq_send(downlink_qd, (const char *)&frame, sizeof(frame), 0)==-1){
				perror("comms: Error sending downlink frame");
				break;
			}
		// 7. TODO: Check if the deadline was missed and, if so, send the report over the telemetry queue
		// SUGGESTION: Use response_time_us(th) to get the response time 
		// SUGGESTION: Use the tlm_pack_deadline function (telemetry.h) to pack the deadline miss telemetry packet
		response_us = response_time_us(th);
		if (response_us > th->deadline) {
			tlm_pack_deadline(&packet, th->index, response_us, th->deadline);
			if (mq_send(tlm_queue, (const char *)&packet, sizeof(struct tlm_packet), 0) == -1 && errno != EAGAIN)
			perror("comms: Error sending telemetry packet");	// queue full: the packet is dropped, the task goes on
		}
	}
	return NULL;
}
