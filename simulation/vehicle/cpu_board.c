/*
 * cpu_board.c - SIMULATION of the CPU board of the lander: its reset line
 *
 * Not software of Pathfinder. The reset restarts this program with execv: every thread,
 * queue, mutex and variable is gone, as the RAM of the lander would be. Before that, the
 * register and the state of the other boards go into the environment, which execv keeps.
 *
 * execv keeps two things of the thread that calls it, here a real-time task: its scheduling
 * (SCHED_FIFO) and its CPU. The new run must start as a normal program, like the first one:
 * so the thread goes back to SCHED_OTHER, on every CPU, before the reset.
 */
#define _GNU_SOURCE		// for the CPU affinity
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cpu_board.h"
#include "transponder.h"	// transponder_data_rate()
#include "imp_card.h"		// imp_card.image

#define KEPT_RESETS  "PATHFINDER_RESETS"
#define KEPT_CYCLE   "PATHFINDER_RESET_CYCLE"

int cpu_board_kept(const char *name, unsigned int *value)
{
	const char *text = getenv(name);
	int kept = 0;

	if (text != NULL && sscanf(text, "%u", value) == 1)
		kept = 1;

	return kept;
}

/* Keep a value across the reset, in the environment */
static void keep(const char *name, unsigned int value)
{
	char text[16];

	snprintf(text, sizeof(text), "%u", value);
	setenv(name, text, 1);
}

int cpu_board_reset_record(struct reset_record *record)
{
	int after_reset = cpu_board_kept(KEPT_RESETS, &record->resets);

	if (after_reset)
		cpu_board_kept(KEPT_CYCLE, &record->cycle);

	return after_reset;
}

void cpu_board_reset(unsigned int cycle)
{
	struct reset_record record = { .resets = 0, .cycle = 0 };
	struct sched_param normal = { .sched_priority = 0 };
	cpu_set_t every_cpu;
	unsigned int image;
	char *program[] = { "pathfinder", NULL };

	// 1. The register, and the state of the other boards
	cpu_board_reset_record(&record);
	keep(KEPT_RESETS, record.resets + 1);
	keep(KEPT_CYCLE, cycle);
	keep(KEPT_DATA_RATE, transponder_data_rate());
	pthread_mutex_lock(&imp_card.lock);
	image = imp_card.image;
	pthread_mutex_unlock(&imp_card.lock);
	keep(KEPT_IMAGE, image);

	// 2. The new run starts as a normal program
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &normal);
	CPU_ZERO(&every_cpu);
	for (long int cpu = 0; cpu < sysconf(_SC_NPROCESSORS_CONF); cpu++)
		CPU_SET(cpu, &every_cpu);
	sched_setaffinity(0, sizeof(every_cpu), &every_cpu);

	// 3. Reset: from main again
	fflush(stdout);
	execv("/proc/self/exe", program);

	perror("cpu board: the reset failed");
	exit(1);
}
