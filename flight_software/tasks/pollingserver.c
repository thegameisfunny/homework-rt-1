#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include "timing.h"		
#include "workload.h"		
#include "telemetry.h"		
#include "tasks.h"		
#include <stdbool.h>			


void* pollingserver_task(void* arg){

	struct pollingserver_str *pollingserver = (struct pollingserver_str *) arg;
	struct periodic_thread *th = &pollingserver->th;

    bool polling_request = false;
    mqd_t polling_queue;
	if ((polling_queue = mq_open(POLLING_QUEUE, O_RDONLY | O_NONBLOCK)) == (mqd_t)-1) {
		perror("pollingserver: mq_open");
		pthread_exit(NULL);
	}

    mqd_t activation_queue;
	if ((activation_queue = mq_open(ACTIVATION_QUEUE, O_WRONLY | O_NONBLOCK)) == (mqd_t)-1) {
		perror("pollingserver: mq_open");
		pthread_exit(NULL);
	}

	// TODO: handle the periodic behavior of the task
	start_periodic_timer(th, POLLING_SERVER_OFFSET);
	//FINE TODO
	while (1) {
		// TODO: handle the next activation of the task
		wait_next_activation(th);
        
        if(mq_receive(polling_queue, (char *)&polling_request, sizeof(bool), 0) > 0){
            polling_request = true;
            mq_send(activation_queue, (const char *)&polling_request, sizeof(bool), 0);//manda l'attivazione al task imp
		}
	}
}
