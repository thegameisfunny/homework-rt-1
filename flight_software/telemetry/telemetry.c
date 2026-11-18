#include "timing.h"			
#include "telemetry.h"

static void start_packet(struct tlm_packet *packet, enum tlm_apid apid)
{
	packet->t_ms = elapsed_ms();
	packet->apid = apid;
	packet->value[0] = 0;
	packet->value[1] = 0;
	packet->value[2] = 0;
	packet->value[3] = 0;
}

void tlm_pack_accelerometers(struct tlm_packet *packet, struct accelerometers_reading *reading)
{
	start_packet(packet, TLM_ACCELEROMETERS);
	packet->value[0] = reading->valid;
	packet->value[1] = reading->x_m_s2;
	packet->value[2] = reading->y_m_s2;
	packet->value[3] = reading->z_m_s2;
}

void tlm_pack_altimeter(struct tlm_packet *packet, struct altimeter_reading *reading)
{
	start_packet(packet, TLM_ALTIMETER);
	packet->value[0] = reading->valid;
	packet->value[1] = reading->altitude_cm;
}

void tlm_pack_asi_met(struct tlm_packet *packet, struct asi_met_reading *reading)
{
	start_packet(packet, TLM_ASI_MET);
	packet->value[0] = reading->temperature_c;
	packet->value[1] = reading->pressure_pa;
}

void tlm_pack_timing(struct tlm_packet *packet, long int response_us, int deadline_us)
{
	start_packet(packet, TLM_TIMING);
	packet->value[0] = response_us / 1000.0f;	// Earth expects ms
	packet->value[1] = deadline_us / 1000;
}

void tlm_pack_deadline(struct tlm_packet *packet, int task, long int response_us, int deadline_us)
{
	start_packet(packet, TLM_DEADLINE);
	packet->value[0] = task;
	packet->value[1] = response_us / 1000;		// Earth expects ms
	packet->value[2] = deadline_us / 1000;
}

void tlm_pack_imp(struct tlm_packet *packet, unsigned int image, long int compression_ms)
{
	start_packet(packet, TLM_IMP);
	packet->value[0] = image;
	packet->value[1] = compression_ms;
}
