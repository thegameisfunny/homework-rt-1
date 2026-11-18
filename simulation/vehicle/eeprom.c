/*
 * eeprom.c - SIMULATION of the non-volatile memory of the lander (EEPROM)
 *
 * Not software of Pathfinder: a small text file of the PC plays the EEPROM, one
 * "name = value" per line.
 */
#include <stdio.h>
#include <string.h>

#include "eeprom.h"

#define EEPROM_LINES  16			// the most lines the EEPROM keeps
#define LINE_MAX_SIZE 160

int eeprom_read(const char *name, char *value, size_t size)
{
	FILE *file;
	char line[LINE_MAX_SIZE];
	char key[64];
	char found_value[96];
	int found = -1;

	file = fopen(EEPROM_FILE, "r");
	if (file != NULL) {
		while (fgets(line, sizeof(line), file) != NULL) {
			if (sscanf(line, " %63[A-Za-z0-9_] = %95[A-Za-z0-9_]", key, found_value) == 2
			    && strcmp(key, name) == 0) {
				snprintf(value, size, "%s", found_value);
				found = 0;
			}
		}
		fclose(file);
	}

	return found;
}

/* The line of 'name' gets the new value: the other lines are kept */
void eeprom_write(const char *name, const char *value)
{
	FILE *file;
	char lines[EEPROM_LINES][LINE_MAX_SIZE];
	char key[64];
	int n = 0;					// lines kept

	file = fopen(EEPROM_FILE, "r");
	if (file != NULL) {
		while (n < EEPROM_LINES - 1 && fgets(lines[n], sizeof(lines[n]), file) != NULL) {
			if (sscanf(lines[n], " %63[A-Za-z0-9_]", key) != 1 || strcmp(key, name) != 0)
				n++;
		}
		fclose(file);
	}

	file = fopen(EEPROM_FILE, "w");
	if (file != NULL) {
		for (int i = 0; i < n; i++)
			fputs(lines[i], file);
		fprintf(file, "%s = %s\n", name, value);
		fclose(file);
	}
}
