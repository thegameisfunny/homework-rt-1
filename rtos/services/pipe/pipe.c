#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "workload.h"		
#include "pipe.h"

#define PIPE_SIZE       32			
#define PIPE_READ_TIME  3000		

static uint8_t message[PIPE_SIZE][PIPE_MESSAGE_MAX];
static size_t length[PIPE_SIZE];		
static unsigned int oldest = 0;			
static unsigned int count = 0;			

static pthread_mutex_t mutexes[2];		
static pthread_mutex_t *mutex;			
static pthread_mutex_t *patched;		
static int mutex_protocol;				
static pthread_mutex_t patch_lock;		

static int writer_waiting = 0;		
static struct timespec writer_since;

static pthread_mutex_t *lock_pipe(void)
{
	pthread_mutex_t *held;				
	pthread_mutex_t *current;			

	pthread_mutex_lock(&patch_lock);
	current = mutex;
	pthread_mutex_unlock(&patch_lock);
	held = current;
	pthread_mutex_lock(held);

	pthread_mutex_lock(&patch_lock);
	current = mutex;
	pthread_mutex_unlock(&patch_lock);
	while (held != current) {
		pthread_mutex_unlock(held);
		held = current;
		pthread_mutex_lock(held);
		pthread_mutex_lock(&patch_lock);
		current = mutex;
		pthread_mutex_unlock(&patch_lock);
	}

	pthread_mutex_lock(&patch_lock);
	if (patched != NULL) {
		pthread_mutex_lock(patched);
		mutex = patched;
		patched = NULL;
		pthread_mutex_unlock(held);
		held = mutex;
	}
	pthread_mutex_unlock(&patch_lock);

	return held;
}

void pipe_write(const void *new_message, size_t size)
{
	unsigned int place;					
	pthread_mutex_t *held;

	pthread_mutex_lock(&patch_lock);
	clock_gettime(CLOCK_REALTIME, &writer_since);
	writer_waiting = 1;
	pthread_mutex_unlock(&patch_lock);

	held = lock_pipe();

	pthread_mutex_lock(&patch_lock);
	writer_waiting = 0;
	pthread_mutex_unlock(&patch_lock);

	if (count < PIPE_SIZE && size <= PIPE_MESSAGE_MAX) {
		place = (oldest + count) % PIPE_SIZE;
		memcpy(message[place], new_message, size);
		length[place] = size;
		count++;
	}

	pthread_mutex_unlock(held);
}

int pipe_read(void *read_message, size_t size)
{
	int found = 0;						
	size_t copy;					
	pthread_mutex_t *held = lock_pipe();

	if (count > 0) {
		copy = length[oldest];
		if (copy > size)
			copy = size;
		memcpy(read_message, message[oldest], copy);
		oldest = (oldest + 1) % PIPE_SIZE;
		count--;
		found = 1;

		compute(PIPE_READ_TIME);
	}

	pthread_mutex_unlock(held);

	return found;
}

void pipe_patch_mutex_protocol(int protocol)
{
	pthread_mutexattr_t attr;
	pthread_mutex_t *spare;				

	pthread_mutex_lock(&patch_lock);
	spare = &mutexes[0];
	if (mutex == &mutexes[0])
		spare = &mutexes[1];
	pthread_mutex_destroy(spare);

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, protocol);
	pthread_mutex_init(spare, &attr);
	pthread_mutexattr_destroy(&attr);

	patched = spare;
	mutex_protocol = protocol;
	pthread_mutex_unlock(&patch_lock);
}

int pipe_writer_waiting(struct timespec *since)
{
	int waiting;

	pthread_mutex_lock(&patch_lock);
	waiting = writer_waiting;
	*since = writer_since;
	pthread_mutex_unlock(&patch_lock);

	return waiting;
}

int pipe_current_mutex_protocol(void)
{
	int protocol;

	pthread_mutex_lock(&patch_lock);
	protocol = mutex_protocol;
	pthread_mutex_unlock(&patch_lock);

	return protocol;
}

int pipe_start(int protocol)
{
	pthread_mutexattr_t attr;
	int error;							

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&patch_lock, &attr);

	pthread_mutexattr_setprotocol(&attr, protocol);
	error = pthread_mutex_init(&mutexes[0], &attr);
	if (error == 0)
		error = pthread_mutex_init(&mutexes[1], &attr);
	pthread_mutexattr_destroy(&attr);

	mutex = &mutexes[0];
	patched = NULL;
	mutex_protocol = protocol;

	return error;
}

void pipe_stop(void)
{
	pthread_mutex_destroy(&mutexes[0]);
	pthread_mutex_destroy(&mutexes[1]);
	pthread_mutex_destroy(&patch_lock);
}
