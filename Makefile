CC     = gcc
CFLAGS = -O2 -Wall -Wextra -Iflight_software -Iflight_software/telemetry -Irtos/system -Irtos/services/timing -Irtos/services/pipe \
         -Irtos/drivers/bus_1553 -Irtos/drivers/radio -Irtos/drivers/imp \
         -Isimulation/workload -Isimulation/vehicle -Isimulation/ground
LDLIBS = -lpthread -lrt -lm

FLIGHT   = flight_software/main.c flight_software/telemetry/telemetry.c flight_software/tasks/bus_scheduler.c flight_software/tasks/distribution.c \
           flight_software/tasks/accelerometers.c flight_software/tasks/altimeter.c flight_software/tasks/comms.c \
           flight_software/tasks/asi_met.c flight_software/tasks/imp.c flight_software/tasks/pollingserver.c 
RTOS     = rtos/system/system.c rtos/services/timing/timing.c rtos/services/pipe/pipe.c rtos/drivers/bus_1553/bus_1553.c rtos/drivers/radio/radio.c rtos/drivers/imp/imp.c
WORKLOAD = simulation/workload/workload.c
VEHICLE  = simulation/vehicle/bus_card.c simulation/vehicle/devices.c simulation/vehicle/transponder.c simulation/vehicle/imp_card.c simulation/vehicle/eeprom.c \
           simulation/vehicle/cpu_board.c
EARTH    = simulation/ground/deep_space_network.c

SOURCES  = $(FLIGHT) $(RTOS) $(WORKLOAD) $(VEHICLE)
HEADERS  = flight_software/schedule.h flight_software/shared_resources.h flight_software/tasks/tasks.h flight_software/telemetry/telemetry.h rtos/system/system.h rtos/services/timing/timing.h rtos/services/pipe/pipe.h \
           rtos/drivers/bus_1553/bus_1553.h rtos/drivers/radio/radio.h rtos/drivers/imp/imp.h simulation/vehicle/bus_card.h simulation/vehicle/devices.h \
           simulation/vehicle/transponder.h simulation/vehicle/imp_card.h simulation/vehicle/eeprom.h simulation/vehicle/cpu_board.h \
           simulation/workload/workload.h

all: pathfinder earth

pathfinder: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

earth: $(EARTH) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(EARTH) -lpthread

lander: pathfinder
	sudo ./pathfinder

ground: earth
ifeq ($(GUI),1)
	python3 simulation/ground/ground_station.py & ./earth; kill $$! 2>/dev/null; true
else
	./earth
endif

clean:
	rm -f pathfinder earth simulation/vehicle/eeprom.txt

.PHONY: all lander ground clean
