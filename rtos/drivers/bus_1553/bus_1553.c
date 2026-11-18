#include <fcntl.h>
#include <math.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include "system.h"			
#include "bus_1553.h"
#include "bus_card.h"

static mqd_t transfer_complete_queue;	

static void complete_interrupt(unsigned int cycle, struct timespec *t1)
{
	struct bus_transfer_complete_msg msg = { .cycle = cycle, .t1 = *t1 };

	mq_send(transfer_complete_queue, (const char *) &msg, sizeof(msg), 0);
}

int bus_1553_start(int cpu)
{
	int error;			

	struct mq_attr transfer_complete_attr = { .mq_maxmsg = 1,
	                                          .mq_msgsize = sizeof(struct bus_transfer_complete_msg) };

	mq_unlink(BUS_TRANSFER_COMPLETE_QUEUE);
	transfer_complete_queue = mq_open(BUS_TRANSFER_COMPLETE_QUEUE, O_CREAT | O_WRONLY | O_NONBLOCK, 0600,
	                                  &transfer_complete_attr);

	error = bus_card_power_on(cpu, system_epoch(), BUS_CYCLE, complete_interrupt);
	return error;
}

void bus_1553_prepare(struct bus_transactions *next)
{
	unsigned int n = 0;						
	unsigned int command[RT_ADDRESSES];		

	if (next->poll_accelerometers) {
		command[n] = RT_ACCELEROMETERS;
		n++;
	}
	if (next->poll_altimeter) {
		command[n] = RT_ALTIMETER;
		n++;
	}
	if (next->poll_asi_met) {
		command[n] = RT_ASI_MET;
		n++;
	}

	pthread_mutex_lock(&bus_card.lock);
	bus_card.n_commands = n;
	memcpy(bus_card.command, command, n * sizeof(command[0]));
	pthread_mutex_unlock(&bus_card.lock);
}

void bus_1553_collect(struct bus_data *data)
{
	unsigned int cycle;						
	struct rt_answer answer[RT_ADDRESSES];		

	pthread_mutex_lock(&bus_card.lock);
	cycle = bus_card.cycle;
	memcpy(answer, bus_card.answer, sizeof(answer));
	pthread_mutex_unlock(&bus_card.lock);

	data->cycle = cycle;

	data->accelerometers.cycle  = cycle;
	data->accelerometers.valid  = answer[RT_ACCELEROMETERS].responded;
	data->accelerometers.x_m_s2 = (int16_t) answer[RT_ACCELEROMETERS].word[0] * ACCELEROMETERS_LSB;
	data->accelerometers.y_m_s2 = (int16_t) answer[RT_ACCELEROMETERS].word[1] * ACCELEROMETERS_LSB;
	data->accelerometers.z_m_s2 = (int16_t) answer[RT_ACCELEROMETERS].word[2] * ACCELEROMETERS_LSB;

	data->altimeter.cycle       = cycle;
	data->altimeter.valid       = answer[RT_ALTIMETER].responded;
	data->altimeter.altitude_cm = answer[RT_ALTIMETER].word[0] * ALTIMETER_LSB;

	data->asi_met.cycle         = cycle;
	data->asi_met.valid         = answer[RT_ASI_MET].responded;
	data->asi_met.temperature_c = lroundf((int16_t) answer[RT_ASI_MET].word[0] * ASI_MET_LSB);
	data->asi_met.pressure_pa   = lroundf(answer[RT_ASI_MET].word[1] * ASI_MET_LSB);
}

void bus_1553_stop(void)
{
	bus_card_power_off();

	mq_close(transfer_complete_queue);
	mq_unlink(BUS_TRANSFER_COMPLETE_QUEUE);
}
