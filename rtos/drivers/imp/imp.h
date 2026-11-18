#ifndef IMP_H
#define IMP_H

#include <stdint.h>
#include <time.h>

#define IMP_IMAGE_SIDE  256

struct imp_image {
	unsigned int number;							
	uint16_t pixel[IMP_IMAGE_SIDE][IMP_IMAGE_SIDE];
};

/* Name of the message queue used to signal that an image is ready */
#define IMP_READY_QUEUE  "/imp_ready_queue"

/* Structure representing the message sent when an image is ready */
struct imp_ready_msg {
	unsigned int image;			
	struct timespec time;		
};

void imp_take_image(void);							
void imp_read_image(struct imp_image *image);	

int  imp_start(int cpu);
void imp_stop(void);

#endif
