#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include "imp.h"
#include "imp_card.h"

static mqd_t ready_queue;		

static void ready_interrupt(unsigned int image)
{
	struct imp_ready_msg msg = { .image = image };

	clock_gettime(CLOCK_REALTIME, &msg.time);
	mq_send(ready_queue, (const char *) &msg, sizeof(msg), 0);
}

void imp_take_image(void)
{
	imp_card_take_image();
}

void imp_read_image(struct imp_image *image)
{
	pthread_mutex_lock(&imp_card.lock);
	image->number = imp_card.image;
	memcpy(image->pixel, imp_card.frame, sizeof(image->pixel));
	pthread_mutex_unlock(&imp_card.lock);
}

int imp_start(int cpu)
{
	int error;			

	struct mq_attr ready_attr = { .mq_maxmsg = 1, .mq_msgsize = sizeof(struct imp_ready_msg) };

	mq_unlink(IMP_READY_QUEUE);
	ready_queue = mq_open(IMP_READY_QUEUE, O_CREAT | O_WRONLY | O_NONBLOCK, 0600, &ready_attr);

	error = imp_card_power_on(cpu, ready_interrupt);
	return error;
}

void imp_stop(void)
{
	imp_card_power_off();

	mq_close(ready_queue);
	mq_unlink(IMP_READY_QUEUE);
}
