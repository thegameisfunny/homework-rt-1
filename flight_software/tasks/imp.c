#include <fcntl.h>
#include <mqueue.h>
#include "timing.h"			
#include "workload.h"		
#include "telemetry.h"		
#include "imp.h"
#include "tasks.h"			

#define IMP_COMPRESSION_TIME  2000000		

void* imp_task(void* arg){

	struct imp_str *imp = (struct imp_str *) arg;

	long int start_ms;
	long int compression_ms;

	// Image structure to hold the captured image
	struct imp_image image;		

	// TODO: Open the IMP ready queue to receive the image ready event from the camera
	mqd_t imp_ready_qd;
	if ((imp_ready_qd = mq_open (IMP_READY_QUEUE, O_RDONLY)) == -1) {
		perror ("imp_task: mq_open (imp_ready)");
		pthread_exit (NULL);
		return NULL;
	}
	struct imp_ready_msg ready;

	// TODO: Open the telemetry queue for sending telemetry packets
	mqd_t tlm_qd;
	if ((tlm_qd = mq_open (TLM_QUEUE, O_WRONLY)) == -1) {
		perror ("imp_task: mq_open (tlm)");
		pthread_exit (NULL);
		return NULL;
	}
	struct tlm_packet packet;

	while (1) {

		// 1. TODO: Ask the camera to take an image
		// SUGGESTION: Look at the functions available in the rtos/drivers/imp/imp.h file
		imp_take_image();
		// 2. TODO: Wait for the image ready event from the camera telling us that the image is available
		mq_receive(imp_ready_qd, (char *)&ready, sizeof(ready), 0);
		// 3. Copy the image from the camera and compress it (compression is simulated using compute)
		start_ms = elapsed_ms();
		// TODO: Read the image from the camera into the image structure
		// SUGGESTION: Look at the functions available in the rtos/drivers/imp/imp.h file
		imp_read_image(&image);
		
		compute(IMP_COMPRESSION_TIME);
		compression_ms = elapsed_ms() - start_ms;

		// 4. Update the image counter and send telemetry about the compression time
		// SUGGESTION: Use the tlm_pack_imp function (telemetry.h) to pack the telemetry packet
		imp->images++;
		tlm_pack_imp(&packet, imp->images, compression_ms);
		if (mq_send(tlm_qd, (const char *)&packet, sizeof(struct tlm_packet), 0)==-1){
			perror("imp: Error sending telemetry packet");
			break;
		}
	}
	return NULL;
}
