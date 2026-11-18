/*
 * transponder.c - SIMULATION of the radio of the lander: transmitter and receiver
 *
 * Not software of Pathfinder: this plays the radio, which works in parallel with the CPU.
 *
 * The transmitter sends one frame at a time, all of the same length: at the current data
 * rate it is ready for the next frame every frame time, and raises an interrupt.
 * transponder_task sleeps until the transmitter is ready, then calls the interrupt handler
 * connected at power on. It works in parallel with the flight software, on its own CPU.
 * A new data rate, set by the driver, is used from the next frame on. The frame times are
 * not multiples of 125 ms.
 *
 * At power on the transponder draws the data rate the session starts with: on a real
 * mission it depends on the antenna on Earth and on the distance. A reset of the CPU does
 * not touch the transponder: after one it keeps its data rate (cpu_board.h).
 *
 * The hardware has its own clock: it uses no service of the operating system. Its clock
 * is not the clock of the computer: at power on the phase between the two is unknown, so
 * the first "ready" comes at a random instant within one frame time, different at every
 * run. After that, one every frame time.
 * Both draws use rand(), seeded with the current time.
 *
 * The receiver: receiver_task waits for the commands of Earth, and for each one calls the
 * other interrupt handler, with the command.
 *
 * The other end of the link is Earth (simulation/ground/deep_space_network.c), another
 * program: the radio waves are UDP datagrams on this computer (struct link_frame). If Earth
 * is not running, the frames are simply lost in space.
 */

#define _GNU_SOURCE		// for the CPU affinity
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "transponder.h"
#include "cpu_board.h"		// after a reset of the CPU: the data rate we had

/*
 * The hardware has a CPU of the PC for itself (not the CPU of the flight software): a high
 * SCHED_FIFO priority, so that nothing else on that CPU delays it
 */
#define TRANSPONDER_PRIORITY  80

static pthread_t thread_transponder;
static pthread_t thread_receiver;
static struct timespec power_on_time;					// when it starts working
static uint64_t high_frame_us;							// frame time at the high data rate

static void (*ready_line)(unsigned int slot);				// the handlers connected by the driver
static void (*command_line)(struct command_msg *command);

/*
 * The data rate: a register of the transponder, written by the driver and read by the
 * transmitter, under its lock (priority inheritance)
 */
static enum data_rate data_rate;
static pthread_mutex_t data_rate_lock;

/* The antenna: towards Earth, and from Earth */
static int downlink_sock;
static struct sockaddr_in earth;					// 127.0.0.1, LINK_DOWNLINK_PORT
static int uplink_sock;								// bound to LINK_UPLINK_PORT

void* transponder_task(void* arg);
void* receiver_task(void* arg);

/* The clock of the transponder: move t forward by us microseconds */
static void clock_add_us(struct timespec *t, uint64_t us)
{
	t->tv_nsec += us * 1000;
	t->tv_sec  += t->tv_nsec / 1000000000L;
	t->tv_nsec %= 1000000000L;
}

/* Time to send one frame at the current data rate [us] */
static uint64_t frame_time_us(void)
{
	uint64_t us;

	pthread_mutex_lock(&data_rate_lock);
	if (data_rate == DATA_RATE_HIGH)
		us = high_frame_us;
	else if (data_rate == DATA_RATE_MEDIUM)
		us = 2 * high_frame_us;
	else
		us = 4 * high_frame_us;
	pthread_mutex_unlock(&data_rate_lock);

	return us;
}

/*
 * Transponder task: until the power goes off (transponder_power_off).
 * arg: the time it starts working.
 */
void* transponder_task(void* arg){

	struct timespec next_ready = *(struct timespec *) arg;	// when it is ready again
	uint64_t phase_us;						// from the start to the first "ready"
	unsigned int slot = 0;					// transmission slots so far

	// The phase between our clock and the clock of the computer: unknown
	phase_us = rand() % frame_time_us();
	clock_add_us(&next_ready, phase_us);

	while (1) {

		// 1. The transmitter is busy with the current frame
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_ready, NULL);

		// 2. Ready for the next frame: raise the interrupt
		slot++;
		ready_line(slot);

		// 3. The next frame is sent at the data rate of now
		clock_add_us(&next_ready, frame_time_us());
	}
}

/*
 * Receiver task: waits for the commands of Earth and raises the interrupt for each one,
 * until the power goes off. A datagram that is not a command is not ours: ignored
 */
void* receiver_task(void* arg){

	(void) arg;
	struct command_msg command;
	ssize_t len;

	while (1) {
		len = recv(uplink_sock, &command, sizeof(command), 0);
		if (len == (ssize_t) sizeof(command))
			command_line(&command);
	}
}

/* The antenna: a socket towards Earth, and one bound to the uplink port. 0, or -1 */
static int antenna_open(void)
{
	struct sockaddr_in uplink;
	int error = 0;

	// SOCK_CLOEXEC: a reset (execv, cpu_board.c) closes them, and the new run opens them again
	downlink_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	memset(&earth, 0, sizeof(earth));
	earth.sin_family      = AF_INET;
	earth.sin_port        = htons(LINK_DOWNLINK_PORT);
	earth.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	uplink_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	memset(&uplink, 0, sizeof(uplink));
	uplink.sin_family      = AF_INET;
	uplink.sin_port        = htons(LINK_UPLINK_PORT);
	uplink.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(uplink_sock, (struct sockaddr *) &uplink, sizeof(uplink)) != 0) {
		fprintf(stderr, "transponder: cannot listen on port %d: is another lander running?\n",
		        LINK_UPLINK_PORT);
		error = -1;
	}

	return error;
}


/*
 * Power on: draw the data rate the session starts with, connect the interrupt handlers,
 * open the antenna, then start the transmitter and the receiver, SCHED_FIFO, on its CPU
 */
int transponder_power_on(int cpu, struct timespec start, uint64_t frame_time_us,
                         void (*ready_handler)(unsigned int slot),
                         void (*command_handler)(struct command_msg *command))
{
	pthread_mutexattr_t mutex_attr;
	pthread_attr_t myattr;
	struct sched_param myparam;
	cpu_set_t transponder_cpu;
	unsigned int kept_rate;
	int error;

	power_on_time = start;
	high_frame_us = frame_time_us;
	ready_line    = ready_handler;
	command_line  = command_handler;

	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&data_rate_lock, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);

	srand(time(NULL));
	if (cpu_board_kept(KEPT_DATA_RATE, &kept_rate) && kept_rate <= DATA_RATE_HIGH)
		data_rate = kept_rate;
	else
		data_rate = rand() % 3;

	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);
	myparam.sched_priority = TRANSPONDER_PRIORITY;
	pthread_attr_setschedparam(&myattr, &myparam);
	CPU_ZERO(&transponder_cpu);
	CPU_SET(cpu, &transponder_cpu);
	pthread_attr_setaffinity_np(&myattr, sizeof(transponder_cpu), &transponder_cpu);

	error = antenna_open();
	if (error == 0)
		error = pthread_create(&thread_transponder, &myattr, transponder_task, &power_on_time);
	if (error == 0)
		error = pthread_create(&thread_receiver, &myattr, receiver_task, NULL);
	pthread_attr_destroy(&myattr);

	return error;
}

/*
 * Power off: the receiver and the transmitter stop (recv is a cancellation point; the
 * transmitter never sleeps holding the lock), then the antenna closes
 */
void transponder_power_off(void)
{
	pthread_cancel(thread_receiver);
	pthread_join(thread_receiver, NULL);
	pthread_cancel(thread_transponder);
	pthread_join(thread_transponder, NULL);
	pthread_mutex_destroy(&data_rate_lock);
	close(uplink_sock);
	close(downlink_sock);
}

void transponder_set_data_rate(enum data_rate new_rate)
{
	pthread_mutex_lock(&data_rate_lock);
	data_rate = new_rate;
	pthread_mutex_unlock(&data_rate_lock);
}

enum data_rate transponder_data_rate(void)
{
	enum data_rate rate;

	pthread_mutex_lock(&data_rate_lock);
	rate = data_rate;
	pthread_mutex_unlock(&data_rate_lock);

	return rate;
}

/* The frame goes to Earth, with its data rate. A frame too long for the link is not sent */
void transponder_transmit(const void *frame, size_t size)
{
	static struct link_frame waves;		// what goes on the air: only the radio task sends

	pthread_mutex_lock(&data_rate_lock);
	waves.rate = data_rate;
	pthread_mutex_unlock(&data_rate_lock);

	if (size <= LINK_FRAME_MAX) {
		waves.size = size;
		memcpy(waves.bytes, frame, size);
		sendto(downlink_sock, &waves, offsetof(struct link_frame, bytes) + size, 0,
		       (struct sockaddr *) &earth, sizeof(earth));
	}
}
