#ifndef SYSTEM_H
#define SYSTEM_H

#include <time.h>

#define RT_CPU  0

enum shell_result {
	SHELL_OK,					
	SHELL_SYNTAX_ERROR,			
	SHELL_UNKNOWN_VARIABLE,		
	SHELL_BAD_VALUE			
};
	
enum system_report_kind {
	SYSTEM_REPORT_SHELL,
	SYSTEM_REPORT_BOOT
};

struct system_report {
	enum system_report_kind kind;
	unsigned int lines;
	int result;
	unsigned int resets;
	unsigned int cycle;
};

struct timespec system_epoch(void);

int system_start(void);
void system_stop(void);
void system_reset(unsigned int cycle);
int system_shell(const char *line);		
int system_boot_report(struct system_report *report);

#endif
