#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <string.h>
#include "timing.h"			
#include "radio.h"
#include "system.h"			
#include "transponder.h"

#define DOWNLINK_MAX  4			

static pthread_t thread_radio;
static mqd_t downlink_queue;			
static mqd_t system_frames;				
static mqd_t tx_ready_queue;			

static struct system_report report;
static int report_waiting = 0;				
static unsigned int patches = 0;			
static pthread_mutex_t report_lock;

void* radio_task(void* arg);

void* radio_task(void* arg){

	mqd_t queue = *(mqd_t *) arg;		

	struct downlink_frame frame;	

	while (1) {

		if (mq_receive(queue, (char *) &frame, sizeof(frame), NULL) >= 0)
			transponder_transmit(&frame, sizeof(frame));
	}
}

static void ready_interrupt(unsigned int slot)
{
	struct radio_tx_ready_msg msg = { .slot = slot };
	struct downlink_frame frame = { .number = slot, .channel = RADIO_CHANNEL_SYSTEM };
	int system_slot = 0;				

	pthread_mutex_lock(&report_lock);
	if (report_waiting) {
		frame.t_ms = elapsed_ms();
		frame.n_bytes = sizeof(report);
		memcpy(frame.data, &report, sizeof(report));
		if (mq_send(system_frames, (const char *) &frame, sizeof(frame), 0) == 0) {
			report_waiting = 0;
			system_slot = 1;
		}
	}
	pthread_mutex_unlock(&report_lock);

	if (system_slot == 0) {
		clock_gettime(CLOCK_REALTIME, &msg.time);
		mq_send(tx_ready_queue, (const char *) &msg, sizeof(msg), 0);
	}
}

static void command_interrupt(struct command_msg *command)
{
	int result;

	if (command->kind == COMMAND_SET_DATA_RATE
	    && command->rate >= DATA_RATE_LOW && command->rate <= DATA_RATE_HIGH)
		transponder_set_data_rate(command->rate);

	if (command->kind == COMMAND_PATCH) {
		command->patch[PATCH_MAX - 1] = '\0';
		result = system_shell(command->patch);
		pthread_mutex_lock(&report_lock);
		patches++;
		report.kind   = SYSTEM_REPORT_SHELL;
		report.lines  = patches;
		report.result = result;
		report.resets = 0;
		report.cycle  = 0;
		report_waiting = 1;
		pthread_mutex_unlock(&report_lock);
	}
}

void frame_start(struct downlink_frame *frame, unsigned int slot)
{
	frame->number  = slot;
	frame->t_ms    = elapsed_ms();
	frame->channel = RADIO_CHANNEL_FLIGHT;
	frame->n_bytes = 0;
}

void frame_add(struct downlink_frame *frame, const void *data, size_t size)
{
	if (frame->n_bytes + size <= RADIO_FRAME_BYTES) {
		memcpy(&frame->data[frame->n_bytes], data, size);
		frame->n_bytes += size;
	}
}

int radio_start(int cpu)
{
	int error;			
	pthread_mutexattr_t mutex_attr;

	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&report_lock, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);
	patches = 0;
	report_waiting = system_boot_report(&report);

	struct mq_attr tx_ready_attr = { .mq_maxmsg = 1,            .mq_msgsize = sizeof(struct radio_tx_ready_msg) };
	struct mq_attr downlink_attr = { .mq_maxmsg = DOWNLINK_MAX, .mq_msgsize = sizeof(struct downlink_frame) };

	mq_unlink(RADIO_TX_READY_QUEUE);
	tx_ready_queue = mq_open(RADIO_TX_READY_QUEUE, O_CREAT | O_WRONLY | O_NONBLOCK, 0600, &tx_ready_attr);

	mq_unlink(RADIO_DOWNLINK_QUEUE);
	downlink_queue = mq_open(RADIO_DOWNLINK_QUEUE, O_CREAT | O_RDONLY, 0600, &downlink_attr);
	system_frames = mq_open(RADIO_DOWNLINK_QUEUE, O_WRONLY | O_NONBLOCK);

	error = transponder_power_on(cpu, system_epoch(), RADIO_MIN_FRAME_TIME, ready_interrupt,
	                             command_interrupt);

	if (error == 0)
		error = pthread_create(&thread_radio, NULL, radio_task, &downlink_queue);

	return error;
}

void radio_stop(void)
{
	pthread_cancel(thread_radio);
	pthread_join(thread_radio, NULL);

	transponder_power_off();

	mq_close(system_frames);
	mq_close(downlink_queue);
	mq_close(tx_ready_queue);
	mq_unlink(RADIO_DOWNLINK_QUEUE);
	mq_unlink(RADIO_TX_READY_QUEUE);
	pthread_mutex_destroy(&report_lock);
}
