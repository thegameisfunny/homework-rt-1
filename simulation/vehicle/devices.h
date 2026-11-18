/*
 * devices.h - SIMULATION of the devices on the 1553 bus: accelerometers, radar altimeter,
 * ASI/MET weather instrument
 *
 * Used only by the bus card (bus_card.c): on a real 1553 bus each device is a "remote
 * terminal", which answers when the card asks for its data.
 */
#ifndef DEVICES_H
#define DEVICES_H

#include <stdint.h>

/*
 * The device at 'address' (bus_card.h) answers: it puts its data words in 'word', in the
 * format of bus_card.h, and returns how many (0 = no device at that address). time_us,
 * from the first cycle, is the time for the device: the weather changes along the Martian
 * day.
 */
unsigned int device_answer(unsigned int address, uint64_t time_us, uint16_t *word);

#endif
