/*
 * bus_card.h - SIMULATION of the 1553 interface card, and of what the devices send on the bus
 *
 * Used only by the bus driver (rtos/drivers/bus_1553/bus_1553.c) and by the devices (devices.c),
 * never by the flight software. The card memory is shared by the hardware and the driver,
 * like the memory of a real interface card.
 *
 * The card knows nothing of the software: it reads devices by their address on the bus
 * and keeps the 16-bit data words they answer. The driver turns the transactions of the
 * software into a command list for the card, and the data words into readings.
 */
#ifndef BUS_CARD_H
#define BUS_CARD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * The devices on the bus: each one is a "remote terminal", with its address
 */
#define RT_ACCELEROMETERS  1
#define RT_ALTIMETER       2
#define RT_ASI_MET         3
#define RT_ADDRESSES       4			// addresses 0 .. 3 (0 is not used)

/*
 * What each device answers: 16-bit data words, in this order and with this scale
 */
#define ACCELEROMETERS_WORDS  3			// x, y, z: signed, 1 = ACCELEROMETERS_LSB
#define ACCELEROMETERS_LSB    0.001f	// [m/s^2]
#define ALTIMETER_WORDS       1			// altitude: unsigned, 1 = ALTIMETER_LSB
#define ALTIMETER_LSB         10		// [cm]
#define ASI_MET_WORDS         2			// temperature: signed, pressure: unsigned
#define ASI_MET_LSB           0.1f		// [C] and [Pa]

#define RT_MAX_WORDS  32				// 1553: at most 32 data words in a message

/*
 * The answer of one device in the last cycle
 */
struct rt_answer {
	bool responded;						// false = not read in the last cycle
	uint16_t word[RT_MAX_WORDS];		// its data words
};

/*
 * The memory of the interface card
 * The driver writes the command list of the next cycle, the hardware runs it at t1 and
 * leaves the answers here at t2. The lock is held only to copy in or out (priority
 * inheritance).
 */
struct bus_card_str {
	unsigned int n_commands;				// written by the driver: how many devices to read
	unsigned int command[RT_ADDRESSES];		// in the next cycle, and their addresses, in order
	unsigned int cycle;						// written by the hardware: the last cycle,
	struct rt_answer answer[RT_ADDRESSES];	// and the answer of each device, by address
	pthread_mutex_t lock;
};
extern struct bus_card_str bus_card;

/*
 * Power on: the card memory is cleared (nothing to run until the driver writes a command
 * list), the interrupt handler of the driver is connected, and the hardware starts
 * working: the first t1 is 'start', then one cycle every cycle_us (the driver programs
 * it). At t2 of every cycle, when the answers are in the card memory, the card calls
 * complete_handler ("traffic complete"), with the bus cycle and its time tag: when the
 * cycle started (t1).
 * Returns 0, or the error of pthread_create (usually: not allowed to use SCHED_FIFO).
 */
int  bus_card_power_on(int cpu, struct timespec start, uint64_t cycle_us,
                       void (*complete_handler)(unsigned int cycle, struct timespec *t1));
void bus_card_power_off(void);

#endif
