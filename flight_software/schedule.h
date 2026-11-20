#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "bus_1553.h"			
#include "radio.h"

/**************************** Time Constants ********************************/

#define T1          0					
#define T4          100000					
#define NEXT_T1		BUS_CYCLE	// Set by the hardware (8 Hz cycle, 125 ms)

/***************************** Task Table ********************************/

#define BUS_SCHEDULER_ID          	0
#define BUS_SCHEDULER_PERIOD      	BUS_CYCLE	
#define BUS_SCHEDULER_OFFSET      	T4		
#define BUS_SCHEDULER_WCET        	5000
#define BUS_SCHEDULER_DEADLINE    	25000		// 25 ms
#define BUS_SCHEDULER_PRIORITY    	60		// Scheduling Deadline Monotonic Priorità più alta (D = 25 ms)

#define DISTRIBUTION_ID           	1
#define DISTRIBUTION_PERIOD       	BUS_CYCLE	
#define DISTRIBUTION_OFFSET       	0		
#define DISTRIBUTION_WCET         	8000
#define DISTRIBUTION_DEADLINE     	100000		// 100 ms
#define DISTRIBUTION_PRIORITY		55		// D = 100 ms

#define ACCELEROMETERS_ID          	2
#define ACCELEROMETERS_PERIOD      	250000
#define ACCELEROMETERS_OFFSET      	60000
#define ACCELEROMETERS_WCET        	3000
#define ACCELEROMETERS_DEADLINE    	250000		// 250 ms
#define ACCELEROMETERS_PRIORITY    	50		// D = 250 ms

#define ALTIMETER_ID              	3
#define ALTIMETER_PERIOD           	500000
#define ALTIMETER_OFFSET          	70000
#define ALTIMETER_WCET              2000
#define ALTIMETER_DEADLINE        	500000		// 500 ms
#define ALTIMETER_PRIORITY     		45		// D = 500 ms

#define COMMS_ID                  	4
#define COMMS_PERIOD              	RADIO_MIN_FRAME_TIME			
#define COMMS_OFFSET                0		
#define COMMS_WCET                  200000
#define COMMS_DEADLINE            	755000		// 755 ms (o RADIO_MIN_FRAME_TIME)
#define COMMS_PRIORITY              40		// D = 755 ms

#define ASI_MET_ID                  5
#define ASI_MET_PERIOD             	1000000
#define ASI_MET_OFFSET              110000	
#define ASI_MET_WCET                20000	
#define ASI_MET_DEADLINE          	1000000		// 1000 ms
#define ASI_MET_PRIORITY            35		// Priorità più bassa tra i task RT (D = 1000 ms)

#define POLLING_SERVER_ID              	6
#define POLLING_SERVER_PERIOD           1200000 // 1200 ms
#define POLLING_SERVER_OFFSET  	        0
#define POLLING_SERVER_CAPACITY        	300000  // 300 ms di CPU per periodo: U = 25%
#define POLLING_SERVER_DEADLINE        	1200000 // D = T = 1200 ms: deadline piu' lunga per aver priorita' piu' bassa (DM)
#define POLLING_SERVER_PRIORITY        	30
#define POLLING_SERVER_CHECK            5000    // ogni 5 ms il server controlla la capacita' consumata da IMP
// Periodo e capacita' piccoli così il server non tiene mai la CPU per troppo tempo evitando il throttling visto a lezione nelle slide 09 sulle SCHED_DEADLINE ed evitiamo quindi comportamenti instabili, forse non è un problema (in test non mi dava noie), però meglio essere prudenti e non rischiare di avere un thread che monopolizza la CPU per troppo tempo, anche se il server ha priorità più bassa dei task RT e soprattutto possiamo gestire effettivamente il tempo del task imp.


#define IMP_ID                  7
#define IMP_PERIOD              0       // non è un task periodico, quindi non ha periodo   
#define IMP_PRIORITY 0
#define IMP_SERVED_PRIORITY     (POLLING_SERVER_PRIORITY - 1)  // priorita' di IMP mentre e' servito dal server
#define IMP_WCET 2000000


#endif
