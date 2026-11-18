/*
 * transponder.h - SIMULATION of the radio of the lander: transmitter and receiver
 *
 * Used only by the radio driver (rtos/drivers/radio/radio.c), never by the flight software.
 * Earth (simulation/ground/deep_space_network.c, another program: make ground) uses only the
 * format of the radio link, below.
 *
 * The transmitter sends one frame every frame time: each time it is ready for the next
 * frame it raises an interrupt. The frame time depends on the data rate, low, medium or
 * high: the session starts at a data rate drawn at random, and Earth can change it with a
 * command. The receiver gets the commands from Earth: for each one it raises another
 * interrupt.
 * The hardware knows nothing of the software: at power on the driver connects its interrupt
 * handlers to the two interrupt lines (in VxWorks: intConnect), and the transponder calls them.
 */
#ifndef TRANSPONDER_H
#define TRANSPONDER_H

#include <time.h>

#include <stddef.h>
#include <stdint.h>

/*
 * The radio link between Mars and Earth: UDP on this computer, one datagram per frame or
 * command. The radio waves carry the frame and its data rate (Earth's receiver locks on to
 * it); the commands go up as they are
 */
#define LINK_DOWNLINK_PORT  5004		// Earth listens here for the frames
#define LINK_UPLINK_PORT    5005		// the transponder listens here for the commands
#define LINK_FRAME_MAX      4096		// the longest frame the link carries [bytes]

struct link_frame {
	int32_t rate;					// the data rate of the frame (enum data_rate)
	uint32_t size;					// bytes of the frame
	uint8_t bytes[LINK_FRAME_MAX];	// the frame: only 'size' bytes travel
};

enum data_rate {
	DATA_RATE_LOW,				// a frame every 4 x the frame time programmed at power on
	DATA_RATE_MEDIUM,			// a frame every 2 x the frame time programmed at power on
	DATA_RATE_HIGH				// a frame every frame time programmed at power on
};

/*
 * A command from Earth, as the receiver delivers it. Earth builds it, the driver executes
 * it: "set the data rate", or a patch, a line of C for the shell of the lander
 */
enum command_kind {
	COMMAND_SET_DATA_RATE,
	COMMAND_PATCH
};

#define PATCH_MAX  96			// the longest line of a patch, with its '\0'

struct command_msg {
	enum command_kind kind;
	int rate;					// COMMAND_SET_DATA_RATE: the new data rate, an enum data_rate
	char patch[PATCH_MAX];		// COMMAND_PATCH: the line of C
};

/*
 * The uplink, from Earth to the lander, is much slower than the downlink: a command takes
 * its bits at this rate (and the time the radio waves travel from Earth to Mars, not
 * simulated)
 */
#define UPLINK_BITS_PER_S  50

/*
 * Power on: the transponder draws the data rate the session starts with (after a reset of
 * the CPU it keeps the one it had: the transponder was never off), and starts
 * working at 'start', with its own clock: the first "ready" comes at a random instant
 * within one frame time. frame_time_us, programmed by the driver, is the time to send one
 * frame at the high data rate. Each time the transmitter is ready it calls ready_handler with
 * the number of the transmission slot (1, 2, 3, ...); each command received from Earth is
 * given to command_handler.
 * Returns 0, or the error of pthread_create (usually: not allowed to use SCHED_FIFO), or -1
 * if the receiver cannot listen on LINK_UPLINK_PORT.
 */
int  transponder_power_on(int cpu, struct timespec start, uint64_t frame_time_us,
                          void (*ready_handler)(unsigned int slot),
                          void (*command_handler)(struct command_msg *command));
void transponder_power_off(void);

/* The data rate of the transmitter: a new one is used from the next frame on */
void transponder_set_data_rate(enum data_rate rate);

/* The data rate now: for the CPU board, which keeps it across a reset (cpu_board.h) */
enum data_rate transponder_data_rate(void);

/*
 * The driver gives the transmitter a frame, size bytes: it is sent to Earth at the current
 * data rate. The transmitter does not know what the bytes mean
 */
void transponder_transmit(const void *frame, size_t size);

#endif
