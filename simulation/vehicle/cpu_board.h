/*
 * cpu_board.h - SIMULATION of the CPU board of the lander: its reset line
 *
 * Used only by the system (rtos/system/system.c) and by the hardware a reset does not touch
 * (transponder.c, imp_card.c), never by the flight software.
 *
 * On the lander a reset restarts the computer: the CPU, its RAM, the operating system and
 * the flight software. Everything in RAM is lost. The other boards keep working: the
 * transponder keeps its data rate, the camera head stays where it was pointing. And the
 * board keeps in a register how many resets there have been, and the cause of the last one.
 *
 * Here the computer is this program: the reset restarts it (execv), and what survives is
 * handed to the new run in the environment of the process.
 */
#ifndef CPU_BOARD_H
#define CPU_BOARD_H

/* What the hardware keeps across a reset, by name */
#define KEPT_DATA_RATE  "PATHFINDER_DATA_RATE"		// the transponder: its data rate
#define KEPT_IMAGE      "PATHFINDER_IMAGE"			// the camera: images taken so far

/* The register of the board: the resets since power on, and the cause of the last one */
struct reset_record {
	unsigned int resets;
	unsigned int cycle;				// the bus cycle distribution did not complete
};

/*
 * Pull the reset line: the program starts again from main. Does not return. Everything in
 * RAM is lost; the register and the state of the other boards are kept
 */
void cpu_board_reset(unsigned int cycle);

/* At boot: 1 = this boot follows a reset (record filled), 0 = power on */
int cpu_board_reset_record(struct reset_record *record);

/* At power on, for the hardware: 1 = 'name' was kept across a reset (value filled), 0 = not */
int cpu_board_kept(const char *name, unsigned int *value);

#endif
