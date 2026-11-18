/*
 * imp_card.h - SIMULATION of the electronics of the IMP camera
 *
 * Used only by the IMP driver (rtos/drivers/imp/imp.c), never by the flight software.
 *
 * On Pathfinder the camera head sat on the mast, and its electronics were boards in the
 * lander chassis, on the VME bus of the computer: they read out the CCD by themselves and
 * keep the image in their frame buffer, where the CPU copies it from. The frame buffer is
 * shared by the hardware and the driver, like the memory of the 1553 card.
 * The hardware knows nothing of the software: it takes an image when commanded, then calls
 * the interrupt handler connected at power on.
 */
#ifndef IMP_CARD_H
#define IMP_CARD_H

#include <pthread.h>
#include <stdint.h>

#define FRAME_SIDE  256			// the frame buffer: 256 x 256 pixels, 12 bits each

/*
 * The memory of the IMP electronics: the last image taken. The hardware writes it at the
 * end of the readout, the driver copies it; the lock is held only to copy (priority
 * inheritance).
 */
struct imp_card_str {
	unsigned int image;							// images taken so far: the number of the last one
	uint16_t frame[FRAME_SIDE][FRAME_SIDE];		// the frame buffer
	pthread_mutex_t lock;
};
extern struct imp_card_str imp_card;

/*
 * Power on: the hardware waits for commands, on its own CPU. Each image is taken and read
 * out in about 2 s, then the hardware calls ready_handler with the number of the image.
 * Returns 0, or the error of pthread_create (usually: not allowed to use SCHED_FIFO).
 */
int  imp_card_power_on(int cpu, void (*ready_handler)(unsigned int image));
void imp_card_power_off(void);

/* The command "take an image": the hardware starts the exposure and the readout */
void imp_card_take_image(void);

#endif
