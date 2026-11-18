#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bus_1553.h"
#include "radio.h"
#include "imp.h"
#include "pipe.h"
#include "eeprom.h"			
#include "cpu_board.h"	
#include "system.h"

#define START_DELAY  500000

static struct timespec epoch;			

#define BOOT_TIME  3000000

static struct reset_record boot = { .resets = 0, .cycle = 0 };	
static int after_reset = 0;				

#define HARDWARE_CPU  (RT_CPU + 1)

int system_start(void)
{
	int error;							
	int pipe_mutex_protocol = PTHREAD_PRIO_NONE;	
	char value[64];
	struct timespec boot_time = { BOOT_TIME / 1000000, BOOT_TIME % 1000000 * 1000 };

	after_reset = cpu_board_reset_record(&boot);
	if (after_reset) {
		printf("rtos: boot after SYSTEM RESET %u\n", boot.resets);
		fflush(stdout);
		nanosleep(&boot_time, NULL);
	}

	if (eeprom_read("rtos_pipe_mutex_protocol", value, sizeof(value)) == 0
	    && strcmp(value, "PTHREAD_PRIO_INHERIT") == 0)
		pipe_mutex_protocol = PTHREAD_PRIO_INHERIT;
	if (pipe_mutex_protocol == PTHREAD_PRIO_INHERIT)
		printf("rtos: pipe mutex protocol PTHREAD_PRIO_INHERIT, patched (from the EEPROM)\n");
	else
		printf("rtos: pipe mutex protocol PTHREAD_PRIO_NONE, as Pathfinder flew\n");

	clock_gettime(CLOCK_REALTIME, &epoch);
	epoch.tv_sec  += START_DELAY / 1000000;
	epoch.tv_nsec += START_DELAY % 1000000 * 1000;
	if (epoch.tv_nsec >= 1000000000L) {
		epoch.tv_sec++;
		epoch.tv_nsec -= 1000000000L;
	}

	error = pipe_start(pipe_mutex_protocol);
	if (error == 0)
		error = radio_start(HARDWARE_CPU);
	if (error == 0)
		error = bus_1553_start(HARDWARE_CPU);
	if (error == 0)
		error = imp_start(HARDWARE_CPU);

	return error;
}

struct timespec system_epoch(void)
{
	return epoch;
}

void system_stop(void)
{
	int pipe_mutex_protocol = pipe_current_mutex_protocol();

	imp_stop();
	bus_1553_stop();
	radio_stop();
	pipe_stop();

	printf("\nrtos: system resets: %u\n", boot.resets);

	if (pipe_mutex_protocol == PTHREAD_PRIO_NONE)
		printf("\nrtos: the pipe mutex has no priority inheritance. Upload the JPL patch from the\n"
		       "shell of the ground station:   rtos_pipe_mutex_protocol = PTHREAD_PRIO_INHERIT;\n");
}

void system_reset(unsigned int cycle)
{
	struct timespec now;
	struct timespec since;				
	double now_ms;					
	double since_ms;
	const char *protocol = "PTHREAD_PRIO_NONE";

	clock_gettime(CLOCK_REALTIME, &now);
	now_ms = (now.tv_sec - epoch.tv_sec) * 1000.0 + (now.tv_nsec - epoch.tv_nsec) / 1e6
	         - cycle * (BUS_CYCLE / 1000.0);
	if (pipe_current_mutex_protocol() == PTHREAD_PRIO_INHERIT)
		protocol = "PTHREAD_PRIO_INHERIT";

	printf("rtos: SYSTEM RESET %u: distribution not complete at t4, cycle %u. Rebooting\n",
	       boot.resets + 1, cycle);
	printf("rtos:   checked at t1 + %.1f ms; pipe mutex %s, ", now_ms, protocol);
	if (pipe_writer_waiting(&since)) {
		since_ms = (since.tv_sec - epoch.tv_sec) * 1000.0 + (since.tv_nsec - epoch.tv_nsec) / 1e6
		           - cycle * (BUS_CYCLE / 1000.0);
		printf("distribution waiting for it since t1 + %.1f ms\n", since_ms);
	}
	else {
		printf("distribution not waiting for it\n");
	}
	cpu_board_reset(cycle);
}

int system_boot_report(struct system_report *report)
{
	if (after_reset) {
		report->kind   = SYSTEM_REPORT_BOOT;
		report->lines  = 0;
		report->result = SHELL_OK;
		report->resets = boot.resets;
		report->cycle  = boot.cycle;
	}

	return after_reset;
}

int system_shell(const char *line)
{
	char variable[64];
	char value[64];
	int end = 0;						
	int protocol = -1;					
	int result = SHELL_SYNTAX_ERROR;

	if (sscanf(line, " %63[A-Za-z0-9_] = %63[A-Za-z0-9_] %n", variable, value, &end) == 2 && end > 0) {
		
		if (line[end] == ';')
			end++;
		while (line[end] == ' ' || line[end] == '\t')
			end++;

		if (line[end] != '\0')
			result = SHELL_SYNTAX_ERROR;
		else if (strcmp(variable, "rtos_pipe_mutex_protocol") != 0)
			result = SHELL_UNKNOWN_VARIABLE;
		else {
			if (strcmp(value, "PTHREAD_PRIO_NONE") == 0)
				protocol = PTHREAD_PRIO_NONE;
			else if (strcmp(value, "PTHREAD_PRIO_INHERIT") == 0)
				protocol = PTHREAD_PRIO_INHERIT;

			if (protocol < 0)
				result = SHELL_BAD_VALUE;
			else {
				pipe_patch_mutex_protocol(protocol);
				eeprom_write(variable, value);		
				result = SHELL_OK;
			}
		}
	}

	return result;
}
