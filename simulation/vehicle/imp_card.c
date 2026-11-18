/*
 * imp_card.c - SIMULATION of the electronics of the IMP camera
 *
 * Not software of Pathfinder: this plays the IMP boards, which work in parallel with the
 * CPU. imp_card_task waits for the command "take an image", then exposes and reads out the
 * CCD: the ADC converts the pixels in about 2 s. It SLEEPS for that time, it does not
 * compute: the readout is done by the hardware, while the CPU of the lander runs the tasks.
 * Then it leaves the image in the frame buffer and raises the "image ready" interrupt.
 *
 * The image is not a real picture: the pixels are a pattern, different for every image.
 * The hardware has its own clock: it uses no service of the operating system.
 */

#define _GNU_SOURCE		// for the CPU affinity
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <time.h>

#include "imp_card.h"
#include "cpu_board.h"		// after a reset of the CPU: the images taken so far

/*
 * The hardware has a CPU of the PC for itself (not the CPU of the flight software): a high
 * SCHED_FIFO priority, so that nothing else on that CPU delays it
 */
#define IMP_CARD_PRIORITY  80

#define READOUT_US  2000000		// exposure and readout of the CCD: about 2 s on the IMP

/*
 * The memory of the IMP electronics (see imp_card.h), shared with the driver
 */
struct imp_card_str imp_card;

static pthread_t thread_imp_card;
static sem_t take_command;							// "take an image", from the driver
static void (*ready_line)(unsigned int image);		// the handler connected by the driver

void* imp_card_task(void* arg);

/*
 * IMP hardware task: until the power goes off (imp_card_power_off).
 * arg: not used.
 */
void* imp_card_task(void* arg){

	struct timespec ready;			// when the readout is over
	unsigned int image = imp_card.image;	// images taken so far: set at power on
	(void) arg;

	while (1) {

		// 1. Wait for the command
		sem_wait(&take_command);

		// 2. Expose and read out the CCD: the hardware works, the CPU of the lander is free
		clock_gettime(CLOCK_REALTIME, &ready);
		ready.tv_sec  += READOUT_US / 1000000;
		ready.tv_nsec += READOUT_US % 1000000 * 1000;
		if (ready.tv_nsec >= 1000000000L) {
			ready.tv_sec++;
			ready.tv_nsec -= 1000000000L;
		}
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ready, NULL);

		// 3. The image in the frame buffer: a 12 bit pattern, different for every image
		image++;
		pthread_mutex_lock(&imp_card.lock);
		imp_card.image = image;
		for (int row = 0; row < FRAME_SIDE; row++)
			for (int column = 0; column < FRAME_SIDE; column++)
				imp_card.frame[row][column] = (row * 16 + column + image * 64) & 0x0fff;
		pthread_mutex_unlock(&imp_card.lock);

		// 4. Raise the "image ready" interrupt
		ready_line(image);
	}
}

/*
 * Power on: the frame buffer is empty, the handler of the driver connected, the hardware
 * started: SCHED_FIFO, on its CPU
 */
int imp_card_power_on(int cpu, void (*ready_handler)(unsigned int image))
{
	pthread_mutexattr_t mutex_attr;
	pthread_attr_t myattr;
	struct sched_param myparam;
	cpu_set_t imp_cpu;
	int error;

	// The lock of the frame buffer, with priority inheritance
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&imp_card.lock, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);

	// After a reset of the CPU the camera goes on from where it was pointing (cpu_board.h)
	if (cpu_board_kept(KEPT_IMAGE, &imp_card.image) == 0)
		imp_card.image = 0;
	sem_init(&take_command, 0, 0);
	ready_line = ready_handler;

	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);
	myparam.sched_priority = IMP_CARD_PRIORITY;
	pthread_attr_setschedparam(&myattr, &myparam);
	CPU_ZERO(&imp_cpu);
	CPU_SET(cpu, &imp_cpu);
	pthread_attr_setaffinity_np(&myattr, sizeof(imp_cpu), &imp_cpu);

	error = pthread_create(&thread_imp_card, &myattr, imp_card_task, NULL);
	pthread_attr_destroy(&myattr);

	return error;
}

/*
 * Power off. The hardware never stops while it holds the lock: it holds it only to write
 * the image, where there is no cancellation point.
 */
void imp_card_power_off(void)
{
	pthread_cancel(thread_imp_card);
	pthread_join(thread_imp_card, NULL);
	sem_destroy(&take_command);
	pthread_mutex_destroy(&imp_card.lock);
}

void imp_card_take_image(void)
{
	sem_post(&take_command);
}
