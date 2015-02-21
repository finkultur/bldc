/*
	Copyright 2012-2014 Benjamin Vedder	benjamin@vedder.se

	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

/*
 * ledpwm.c
 *
 *  Created on: 3 nov 2012
 *      Author: benjamin
 */

#include "ledpwm.h"
#include "ch.h"
#include "hal.h"
#include <string.h>
#include <math.h>
#include "hw.h"

// Private variables
static volatile int led_values[LEDPWM_LED_NUM];
static uint8_t gamma_table[LEDPWM_CNT_TOP + 1];

void ledpwm_init(void) {
	memset((int*)led_values, 0, sizeof(led_values));

	// Generate gamma correction table
	for (int i = 0;i < (LEDPWM_CNT_TOP + 1);i++) {
		gamma_table[i] = (int)roundf(powf((float)i / (float)LEDPWM_CNT_TOP, 1.0 / 0.45) * (float)LEDPWM_CNT_TOP);
	}
}

/*
 * Set the intensity for one led. The intensity value is mapped to a PWM value
 * according to human luminance perception.
 *
 * Intensity range is 0.0 to 1.0
 */
void ledpwm_set_intensity(unsigned int led, float intensity) {
	if (led >= LEDPWM_LED_NUM) {
		return;
	}

	if (intensity < 0.0) {
		intensity = 0.0;
	}

	if (intensity > 1.0) {
		intensity = 1.0;
	}

	led_values[led] = gamma_table[(int)(intensity * LEDPWM_CNT_TOP)];
}

void ledpwm_led_on(int led) {
	if (led >= LEDPWM_LED_NUM) {
		return;
	}

	led_values[led] = LEDPWM_CNT_TOP;
}

void ledpwm_led_off(int led) {
	if (led >= LEDPWM_LED_NUM) {
		return;
	}

	led_values[led] = 0;
}

/*
 * Call this function as fast as possible, with a deterministic rate.
 */
void ledpwm_update_pwm(void) {
	static int cnt = 0;
	cnt++;
	if (cnt == LEDPWM_CNT_TOP) {
		cnt = 0;
	}

	if (cnt >= led_values[0]) {
		LED1_OFF();
	} else {
		LED1_ON();
	}

	if (cnt >= led_values[1]) {
		LED2_OFF();
	} else {
		LED2_ON();
	}
}
