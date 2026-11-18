/*
 * bus_card.c - SIMULATION of the 1553 interface card
 *
 * Not software of Pathfinder: this plays the 1553 interface card, which works in parallel
 * with the CPU. Only the bus driver (rtos/drivers/bus_1553/bus_1553.c) talks to it, through the card
 * memory and the interrupt handler it connects at power on.
 *
 * At t1 of each cycle bus_1553_task runs the command list written by the driver: it asks
 * each device (devices.c) for its data words, by address. At t2, traffic complete, it
 * leaves the answers in the card memory and raises the "traffic complete" interrupt: it
 * calls the handler of the driver.
 *
 * Each transaction takes the time it would take on a real 1553 bus. The task SLEEPS for
 * that time, it does not compute: the transfer is done by the interface card, in parallel
 * with the CPU, which meanwhile is free for the other tasks.
 *
 * The hardware has its own clock: it uses no service of the operating system.
 */

#define _GNU_SOURCE		// for the CPU affinity
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "bus_card.h"
#include "devices.h"

/*
 * The hardware has a CPU of the PC for itself (not the CPU of the flight software): a high
 * SCHED_FIFO priority, so that nothing else on that CPU delays it
 */
#define BUS_CARD_PRIORITY  80

/*
 * The memory of the interface card (see bus_card.h), shared with the driver
 */
struct bus_card_str bus_card;

/*
 * The bus hardware task
 */
static pthread_t thread_bus_1553;
static struct timespec power_on_time;		// the first t1
static uint64_t cycle_us;					// the cycle, programmed by the driver [us]

static void (*complete_line)(unsigned int cycle, struct timespec *t1);	// the handler connected
																		// by the driver

void* bus_1553_task(void* arg);


/*
 * 1553 timing: 1 Mbit/s, every word is 20 bits (3 sync + 16 data + 1 parity) -> 20 us.
 * To read a device the bus controller sends a command word, the device (a "remote
 * terminal") answers after a short gap with a status word and its data words.
 */
#define WORD_US          20		// one word on the bus
#define RESPONSE_GAP_US   8		// the remote terminal answers 4..12 us after the command
#define MESSAGE_GAP_US    4		// minimum gap before the next transaction

/* Duration of a read transaction with 'data_words' data words [us] */
static unsigned int read_transaction_us(unsigned int data_words)
{
	unsigned int duration = 0;

	duration += WORD_US;					// command word, from the bus controller
	duration += RESPONSE_GAP_US;			// the remote terminal gets ready
	duration += WORD_US;					// status word, from the remote terminal
	duration += data_words * WORD_US;		// the data
	duration += MESSAGE_GAP_US;				// the bus is free again

	return duration;
}

/* The clock of the card: move t forward by us microseconds */
static void clock_add_us(struct timespec *t, uint64_t us)
{
	t->tv_nsec += us * 1000;
	t->tv_sec  += t->tv_nsec / 1000000000L;
	t->tv_nsec %= 1000000000L;
}

/* Wait until the transaction that starts at bus_time is over, and move bus_time to its end */
static void wait_transaction(struct timespec *bus_time, unsigned int data_words)
{
	clock_add_us(bus_time, read_transaction_us(data_words));
	clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, bus_time, NULL);
}

/*
 * Bus hardware task: until the power goes off (bus_card_power_off).
 * arg: the time of the first t1.
 */
void* bus_1553_task(void* arg){

	struct timespec next_t1 = *(struct timespec *) arg;	// start of the next cycle
	struct timespec t1;				// start of this cycle: the time tag of its traffic
	unsigned int cycle = 0;			// current bus cycle
	unsigned int n_commands;		// the command list of this cycle
	unsigned int command[RT_ADDRESSES];
	struct rt_answer answer[RT_ADDRESSES];	// the answers of this cycle, by address
	unsigned int address;			// of the device being read
	unsigned int words;				// data words it answered
	struct timespec bus_time;		// end of the last transaction on the bus

	while (1) {

		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_t1, NULL);
		t1 = next_t1;
		clock_add_us(&next_t1, cycle_us);

		// 1. t1: take the command list written in the previous cycle
		pthread_mutex_lock(&bus_card.lock);
		n_commands = bus_card.n_commands;
		memcpy(command, bus_card.command, sizeof(command));
		pthread_mutex_unlock(&bus_card.lock);

		// 2. Read the devices one after the other, starting now: each transaction takes the
		//    time of the transfer on the bus. A device not read has no answer
		clock_gettime(CLOCK_REALTIME, &bus_time);
		memset(answer, 0, sizeof(answer));

		for (unsigned int i = 0; i < n_commands; i++) {
			address = command[i];
			words = device_answer(address, cycle * cycle_us, answer[address].word);
			wait_transaction(&bus_time, words);
			if (words > 0)
				answer[address].responded = true;
		}

		// 3. t2, traffic complete: leave the answers in the card memory
		pthread_mutex_lock(&bus_card.lock);
		bus_card.cycle = cycle;
		memcpy(bus_card.answer, answer, sizeof(answer));
		pthread_mutex_unlock(&bus_card.lock);

		// 4. Raise the "traffic complete" interrupt, with the time tag of the cycle
		complete_line(cycle, &t1);

		cycle++;
	}
}


/*
 * Power on: clear the card memory, then start the hardware: SCHED_FIFO, on its CPU
 */
int bus_card_power_on(int cpu, struct timespec start, uint64_t new_cycle_us,
                      void (*complete_handler)(unsigned int cycle, struct timespec *t1))
{
	pthread_mutexattr_t mutex_attr;
	pthread_attr_t myattr;
	struct sched_param myparam;
	cpu_set_t bus_cpu;
	int error;

	// The card lock, with priority inheritance
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&bus_card.lock, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);

	power_on_time = start;
	cycle_us      = new_cycle_us;
	complete_line = complete_handler;

	// Nothing to run until the driver writes a command list, no answers yet
	bus_card.n_commands = 0;
	bus_card.cycle      = 0;
	memset(bus_card.answer, 0, sizeof(bus_card.answer));

	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);
	myparam.sched_priority = BUS_CARD_PRIORITY;
	pthread_attr_setschedparam(&myattr, &myparam);
	CPU_ZERO(&bus_cpu);
	CPU_SET(cpu, &bus_cpu);
	pthread_attr_setaffinity_np(&myattr, sizeof(bus_cpu), &bus_cpu);

	error = pthread_create(&thread_bus_1553, &myattr, bus_1553_task, &power_on_time);
	pthread_attr_destroy(&myattr);

	return error;
}

/*
 * Power off. The hardware never stops while it holds the card lock: it holds it only to
 * copy, where there is no cancellation point.
 */
void bus_card_power_off(void)
{
	pthread_cancel(thread_bus_1553);
	pthread_join(thread_bus_1553, NULL);
	pthread_mutex_destroy(&bus_card.lock);
}
