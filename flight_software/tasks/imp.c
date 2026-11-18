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
	struct imp_ready_msg ready;

	// TODO: Open the telemetry queue for sending telemetry packets
	struct tlm_packet packet;

	while (1) {

		// 1. TODO: Ask the camera to take an image
		// SUGGESTION: Look at the functions available in the rtos/drivers/imp/imp.h file

		// 2. TODO: Wait for the image ready event from the camera telling us that the image is available

		// 3. Copy the image from the camera and compress it (compression is simulated using compute)
		start_ms = elapsed_ms();
		// TODO: Read the image from the camera into the image structure
		// SUGGESTION: Look at the functions available in the rtos/drivers/imp/imp.h file
		compute(IMP_COMPRESSION_TIME);
		compression_ms = elapsed_ms() - start_ms;

		// 4. Update the image counter and send telemetry about the compression time
		// SUGGESTION: Use the tlm_pack_imp function (telemetry.h) to pack the telemetry packet
	}
}
