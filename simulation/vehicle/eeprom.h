/*
 * eeprom.h - SIMULATION of the non-volatile memory of the lander (EEPROM)
 *
 * Used only by the system (rtos/system/system.c), never by the flight software.
 *
 * What is written here survives a restart, like on the real lander: the system reads its
 * options from here when it starts, and saves here the patches uploaded from Earth. Here it
 * is a small text file of the PC, one "name = value" per line. make clean erases it: back to
 * the factory settings.
 */
#ifndef EEPROM_H
#define EEPROM_H

#include <stddef.h>

#define EEPROM_FILE  "simulation/vehicle/eeprom.txt"	// from the folder of the Makefile

int  eeprom_read(const char *name, char *value, size_t size);	// 0 = found, -1 = not there
void eeprom_write(const char *name, const char *value);

#endif
