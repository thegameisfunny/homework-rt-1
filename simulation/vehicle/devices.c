/*
 * devices.c - SIMULATION of the devices on the 1553 bus
 *
 * The lander is at rest on the surface, slightly tilted. The radar altimeter would read 0
 * (we are on the ground), but on Mars it is off. The weather follows a Martian day.
 * Each device answers with 16-bit data words, in the format of bus_card.h.
 */
#define _GNU_SOURCE		// for M_PI
#include <math.h>
#include <stdlib.h>

#include "bus_card.h"			// the addresses, the format of the data words
#include "devices.h"

/*
 * Accelerometers: gravity on Mars (3.71 m/s^2) on z, a little on x and y because the
 * lander is tilted, plus a random noise on each axis (rand(): seeded by the transponder at
 * power on)
 */
#define TILT_X_M_S2    0.120f
#define TILT_Y_M_S2   -0.080f
#define GRAVITY_M_S2   3.708f
#define NOISE_M_S2     0.020f			// the noise: up to this much, either way

/*
 * ASI/MET: the weather follows a Martian day, compressed into one minute: coldest before
 * dawn, warmest in the early afternoon. The pressure goes the opposite way, a few Pa.
 */
#define MARS_DAY         60000000		// [us] a Martian day (24 h 40 min) in 60 s

#define TEMPERATURE_MEAN_C  -43			// Pathfinder measured about -75 .. -10 C
#define TEMPERATURE_SWING_C  33
#define PRESSURE_MEAN_PA    670			// about 6.7 mbar at the landing site
#define PRESSURE_SWING_PA     5

unsigned int device_answer(unsigned int address, uint64_t time_us, uint16_t *word)
{
	unsigned int words = 0;		// data words of the answer: 0 = nobody at this address
	float noise_x, noise_y, noise_z;	// [m/s^2]
	float day_phase;			// time of the Martian day, 0 .. 2 pi

	if (address == RT_ACCELEROMETERS) {
		noise_x = (rand() % 2001 - 1000) / 1000.0f * NOISE_M_S2;
		noise_y = (rand() % 2001 - 1000) / 1000.0f * NOISE_M_S2;
		noise_z = (rand() % 2001 - 1000) / 1000.0f * NOISE_M_S2;
		word[0] = (uint16_t) lroundf((TILT_X_M_S2  + noise_x) / ACCELEROMETERS_LSB);
		word[1] = (uint16_t) lroundf((TILT_Y_M_S2  + noise_y) / ACCELEROMETERS_LSB);
		word[2] = (uint16_t) lroundf((GRAVITY_M_S2 + noise_z) / ACCELEROMETERS_LSB);
		words = ACCELEROMETERS_WORDS;
	}
	else if (address == RT_ALTIMETER) {
		word[0] = 0;					// on the ground
		words = ALTIMETER_WORDS;
	}
	else if (address == RT_ASI_MET) {
		day_phase = 2 * M_PI * (time_us % MARS_DAY) / MARS_DAY;
		word[0] = (uint16_t) lroundf((TEMPERATURE_MEAN_C + TEMPERATURE_SWING_C * sinf(day_phase)) / ASI_MET_LSB);
		word[1] = (uint16_t) lroundf((PRESSURE_MEAN_PA   - PRESSURE_SWING_PA   * sinf(day_phase)) / ASI_MET_LSB);
		words = ASI_MET_WORDS;
	}

	return words;
}
