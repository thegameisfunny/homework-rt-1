#ifndef BUS_1553_H
#define BUS_1553_H

#include <stdbool.h>
#include <time.h>

#define BUS_CYCLE          125000		

/* Structures representing the readings from various sensors and bus transactions */

struct accelerometers_reading {
	unsigned int cycle;			
	bool valid;
	float x_m_s2;				
	float y_m_s2;
	float z_m_s2;
};

struct altimeter_reading {
	unsigned int cycle;			
	bool valid;
	int altitude_cm;			
};

struct asi_met_reading {		
	unsigned int cycle;			
	bool valid;
	int temperature_c;		
	int pressure_pa;			
};

/* Structure representing the bus transactions to be performed in the next cycle */
struct bus_transactions {
	bool poll_accelerometers;
	bool poll_altimeter;
	bool poll_asi_met;
};

/* Structure representing the data collected from the bus for a single cycle */
struct bus_data {
	unsigned int cycle;			
	struct accelerometers_reading accelerometers;
	struct altimeter_reading altimeter;
	struct asi_met_reading asi_met;
};

/* Name of the message queue used to signal the completion of a bus transfer */	
#define BUS_TRANSFER_COMPLETE_QUEUE  "/bus_transfer_complete_queue"

/* Structure representing the message sent when a bus transfer is complete */
struct bus_transfer_complete_msg {
	unsigned int cycle;			// bus cycle whose traffic is complete: 0, 1, 2, ...
	struct timespec t1;			// when the cycle started
};														
													
void bus_1553_prepare(struct bus_transactions *next);		
void bus_1553_collect(struct bus_data *data);

int  bus_1553_start(int cpu);		
void bus_1553_stop(void);

#endif
