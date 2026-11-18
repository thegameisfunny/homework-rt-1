#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "bus_1553.h"		

enum tlm_apid {
	TLM_ACCELEROMETERS,			
	TLM_ALTIMETER,				
	TLM_ASI_MET,				
	TLM_TIMING,					
	TLM_DEADLINE,				
	TLM_IMP						
};

struct tlm_packet {
	long int t_ms;			
	enum tlm_apid apid;			
	float value[4];				
};

void tlm_pack_accelerometers(struct tlm_packet *packet, struct accelerometers_reading *reading);
void tlm_pack_altimeter(struct tlm_packet *packet, struct altimeter_reading *reading);
void tlm_pack_asi_met(struct tlm_packet *packet, struct asi_met_reading *reading);
void tlm_pack_imp(struct tlm_packet *packet, unsigned int image, long int compression_ms);

void tlm_pack_timing(struct tlm_packet *packet, long int response_us, int deadline_us);
void tlm_pack_deadline(struct tlm_packet *packet, int task, long int response_us, int deadline_us);

#endif
