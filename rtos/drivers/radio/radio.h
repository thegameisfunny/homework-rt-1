#ifndef RADIO_H
#define RADIO_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define RADIO_MIN_FRAME_TIME  755000		

/* Name of the message queue used to signal that a transmitter is ready */
#define RADIO_TX_READY_QUEUE  "/radio_tx_ready_queue"

/* Structure representing the message sent when a transmission slot is ready */
struct radio_tx_ready_msg {
	unsigned int slot;			
	struct timespec time;		
};

/* Name of the message queue used to signal that a downlink frame is ready */
#define RADIO_DOWNLINK_QUEUE  "/radio_downlink_queue"

#define RADIO_FRAME_BYTES  2048				
#define RADIO_CHANNEL_FLIGHT  0					
#define RADIO_CHANNEL_SYSTEM  1				

/* Structure representing a downlink frame */
struct downlink_frame {
	unsigned int number;				
	long int t_ms;						
	unsigned int channel;				
	unsigned int n_bytes;
	uint8_t data[RADIO_FRAME_BYTES];
};

void frame_start(struct downlink_frame *frame, unsigned int slot);
void frame_add(struct downlink_frame *frame, const void *data, size_t size);	
																			
int radio_start(int cpu);
void radio_stop(void);

#endif
