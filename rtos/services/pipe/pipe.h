#ifndef PIPE_H
#define PIPE_H

#include <stddef.h>
#include <time.h>

#define PIPE_MESSAGE_MAX  64		

void pipe_write(const void *message, size_t size);
int  pipe_read(void *message, size_t size);

int  pipe_start(int pipe_mutex_protocol);									
void pipe_stop(void);

void pipe_patch_mutex_protocol(int protocol);
int  pipe_current_mutex_protocol(void);		
int  pipe_writer_waiting(struct timespec *since);

#endif
