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
 * commands.c
 *
 *  Created on: 19 sep 2014
 *      Author: benjamin
 */

#include "commands.h"
#include "ch.h"
#include "hal.h"
#include "main.h"
#include "stm32f4xx_conf.h"
#include "servo.h"
#include "buffer.h"
#include "myUSB.h"
#include "terminal.h"
#include "hw.h"
#include "mcpwm.h"
#include "app.h"
#include "timeout.h"
#include "servo_dec.h"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// Threads
static msg_t detect_thread(void *arg);
static WORKING_AREA(detect_thread_wa, 2048);
static Thread *detect_tp;

// Private variables
static uint8_t send_buffer[256];
static float detect_cycle_int_limit;
static float detect_coupling_k;
static float detect_current;
static float detect_min_rpm;
static float detect_low_duty;
static void(*send_func)(unsigned char *data, unsigned char len) = 0;

static void send_packet(unsigned char *data, unsigned char len) {
	if (send_func) {
		send_func(data, len);
	}
}

void commands_init(void) {
	chThdCreateStatic(detect_thread_wa, sizeof(detect_thread_wa), NORMALPRIO, detect_thread, NULL);
}

/**
 * Provide a function to use the next time there are packets to be sent.
 *
 * @param func
 * A pointer to the packet sending function.
 */
void commands_set_send_func(void(*func)(unsigned char *data, unsigned char len)) {
	send_func = func;
}

/**
 * Process a received buffer with commands and data.
 *
 * @param data
 * The buffer to process.
 *
 * @param len
 * The length of the buffer.
 */
void commands_process_packet(unsigned char *data, unsigned char len) {
	if (!len) {
		return;
	}

	COMM_PACKET_ID packet_id;
	int32_t ind = 0;
	uint16_t sample_len;
	uint8_t decimation;
	bool at_start;
	mc_configuration mcconf;
	app_configuration appconf;

	(void)len;

	packet_id = data[0];
	data++;
	len--;

	switch (packet_id) {
	case COMM_GET_VALUES:
		ind = 0;
		send_buffer[ind++] = COMM_GET_VALUES;
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS1) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS2) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS3) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS4) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS5) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_MOS6) * 10.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(NTC_TEMP(ADC_IND_TEMP_PCB) * 10.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_read_reset_avg_motor_current() * 100.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_read_reset_avg_input_current() * 100.0), &ind);
		buffer_append_int16(send_buffer, (int16_t)(mcpwm_get_duty_cycle_now() * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)mcpwm_get_rpm(), &ind);
		buffer_append_int16(send_buffer, (int16_t)(GET_INPUT_VOLTAGE() * 10.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_get_amp_hours(false) * 10000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_get_amp_hours_charged(false) * 10000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_get_watt_hours(false) * 10000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcpwm_get_watt_hours_charged(false) * 10000.0), &ind);
		buffer_append_int32(send_buffer, mcpwm_get_tachometer_value(false), &ind);
		buffer_append_int32(send_buffer, mcpwm_get_tachometer_abs_value(false), &ind);
		send_buffer[ind++] = mcpwm_get_fault();
		send_packet(send_buffer, ind);
		break;

	case COMM_SET_DUTY:
		ind = 0;
		mcpwm_set_duty((float)buffer_get_int32(data, &ind) / 100000.0);
		timeout_reset();
		break;

	case COMM_SET_CURRENT:
		ind = 0;
		mcpwm_set_current((float)buffer_get_int32(data, &ind) / 1000.0);
		timeout_reset();
		break;

	case COMM_SET_CURRENT_BRAKE:
		ind = 0;
		mcpwm_set_brake_current((float)buffer_get_int32(data, &ind) / 1000.0);
		timeout_reset();
		break;

	case COMM_SET_RPM:
		ind = 0;
		mcpwm_set_pid_speed((float)buffer_get_int32(data, &ind));
		timeout_reset();
		break;

	case COMM_SET_DETECT:
		mcpwm_set_detect();
		timeout_reset();
		break;

	case COMM_SET_SERVO_OFFSET:
		servos[0].offset = data[0];
		break;

  case COMM_SERVO_MOVE:
    // Not implemented
    break;

  case COMM_SERVO_MOVE_WITHIN_TIME:
    // Not implemented
    break;

  case COMM_SERVO_RESET_POS:
    // Not implemented
    break;

	case COMM_SET_MCCONF:
		mcconf = *mcpwm_get_configuration();

		ind = 0;
		mcconf.pwm_mode = data[ind++];
		mcconf.comm_mode = data[ind++];

		mcconf.l_current_max = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_current_min = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_in_current_max = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_in_current_min = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_abs_current_max = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_min_erpm = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_max_erpm = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_max_erpm_fbrake = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_min_vin = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_max_vin = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_slow_abs_current = data[ind++];
		mcconf.l_rpm_lim_neg_torque = data[ind++];
		mcconf.l_temp_fet_start = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_temp_fet_end = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_temp_motor_start = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.l_temp_motor_end = (float)buffer_get_int32(data, &ind) / 1000.0;

		mcconf.lo_current_max = mcconf.l_current_max;
		mcconf.lo_current_min = mcconf.l_current_min;
		mcconf.lo_in_current_max = mcconf.l_in_current_max;
		mcconf.lo_in_current_min = mcconf.l_in_current_min;

		mcconf.sl_is_sensorless = data[ind++];
		mcconf.sl_min_erpm = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.sl_min_erpm_cycle_int_limit = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.sl_cycle_int_limit = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.sl_cycle_int_limit_high_fac = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.sl_cycle_int_rpm_br = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.sl_bemf_coupling_k = (float)buffer_get_int32(data, &ind) / 1000.0;

		mcconf.hall_dir = data[ind++];
		mcconf.hall_fwd_add = data[ind++];
		mcconf.hall_rev_add = data[ind++];

		mcconf.s_pid_kp = (float)buffer_get_int32(data, &ind) / 1000000.0;
		mcconf.s_pid_ki = (float)buffer_get_int32(data, &ind) / 1000000.0;
		mcconf.s_pid_kd = (float)buffer_get_int32(data, &ind) / 1000000.0;
		mcconf.s_pid_min_rpm = (float)buffer_get_int32(data, &ind) / 1000.0;

		mcconf.cc_startup_boost_duty = (float)buffer_get_int32(data, &ind) / 1000000.0;
		mcconf.cc_min_current = (float)buffer_get_int32(data, &ind) / 1000.0;
		mcconf.cc_gain = (float)buffer_get_int32(data, &ind) / 1000000.0;

		mcconf.m_fault_stop_time_ms = buffer_get_int32(data, &ind);

		conf_general_store_mc_configuration(&mcconf);
		mcpwm_set_configuration(&mcconf);
		break;

	case COMM_GET_MCCONF:
		mcconf = *mcpwm_get_configuration();

		ind = 0;
		send_buffer[ind++] = COMM_GET_MCCONF;

		send_buffer[ind++] = mcconf.pwm_mode;
		send_buffer[ind++] = mcconf.comm_mode;

		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_current_max * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_current_min * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_in_current_max * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_in_current_min * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_abs_current_max * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_min_erpm * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_max_erpm * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_max_erpm_fbrake * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_min_vin * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_max_vin * 1000.0), &ind);
		send_buffer[ind++] = mcconf.l_slow_abs_current;
		send_buffer[ind++] = mcconf.l_rpm_lim_neg_torque;
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_temp_fet_start * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_temp_fet_end * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_temp_motor_start * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.l_temp_motor_end * 1000.0), &ind);

		send_buffer[ind++] = mcconf.sl_is_sensorless;
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_min_erpm * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_min_erpm_cycle_int_limit * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_cycle_int_limit * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_cycle_int_limit_high_fac * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_cycle_int_rpm_br * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.sl_bemf_coupling_k * 1000.0), &ind);

		send_buffer[ind++] = mcconf.hall_dir;
		send_buffer[ind++] = mcconf.hall_fwd_add;
		send_buffer[ind++] = mcconf.hall_rev_add;

		buffer_append_int32(send_buffer, (int32_t)(mcconf.s_pid_kp * 1000000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.s_pid_ki * 1000000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.s_pid_kd * 1000000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.s_pid_min_rpm * 1000.0), &ind);

		buffer_append_int32(send_buffer, (int32_t)(mcconf.cc_startup_boost_duty * 1000000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.cc_min_current * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(mcconf.cc_gain * 1000000.0), &ind);

		buffer_append_int32(send_buffer, mcconf.m_fault_stop_time_ms, &ind);

		send_packet(send_buffer, ind);
		break;

	case COMM_SET_APPCONF:
		appconf = *app_get_configuration();

		ind = 0;
		appconf.timeout_msec = buffer_get_uint32(data, &ind);
		appconf.timeout_brake_current = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_to_use = data[ind++];

		appconf.app_ppm_ctrl_type = data[ind++];
		appconf.app_ppm_pid_max_erpm = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_ppm_hyst = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_ppm_pulse_start = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_ppm_pulse_width = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_ppm_rpm_lim_start = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_ppm_rpm_lim_end = (float)buffer_get_int32(data, &ind) / 1000.0;

		appconf.app_uart_baudrate = buffer_get_uint32(data, &ind);

		appconf.app_chuk_ctrl_type = data[ind++];
		appconf.app_chuk_hyst = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_chuk_rpm_lim_start = (float)buffer_get_int32(data, &ind) / 1000.0;
		appconf.app_chuk_rpm_lim_end = (float)buffer_get_int32(data, &ind) / 1000.0;

		conf_general_store_app_configuration(&appconf);
		app_set_configuration(&appconf);
		timeout_configure(appconf.timeout_msec, appconf.timeout_brake_current);
		break;

	case COMM_GET_APPCONF:
		appconf = *app_get_configuration();

		ind = 0;
		send_buffer[ind++] = COMM_GET_APPCONF;
		buffer_append_uint32(send_buffer, appconf.timeout_msec, &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.timeout_brake_current * 1000.0), &ind);
		send_buffer[ind++] = appconf.app_to_use;

		send_buffer[ind++] = appconf.app_ppm_ctrl_type;
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_pid_max_erpm * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_hyst * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_pulse_start * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_pulse_width * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_rpm_lim_start * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_ppm_rpm_lim_end * 1000.0), &ind);

		buffer_append_uint32(send_buffer, appconf.app_uart_baudrate, &ind);

		send_buffer[ind++] = appconf.app_chuk_ctrl_type;
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_chuk_hyst * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_chuk_rpm_lim_start * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(appconf.app_chuk_rpm_lim_end * 1000.0), &ind);

		send_packet(send_buffer, ind);
		break;

	case COMM_SAMPLE_PRINT:
		ind = 0;
		at_start = data[ind++];
		sample_len = buffer_get_uint16(data, &ind);
		decimation = data[ind++];
		main_sample_print_data(at_start, sample_len, decimation);
		break;

	case COMM_TERMINAL_CMD:
		data[len] = '\0';
		terminal_process_string((char*)data);
		break;

	case COMM_DETECT_MOTOR_PARAM:
		ind = 0;
		detect_current = (float)buffer_get_int32(data, &ind) / 1000.0;
		detect_min_rpm = (float)buffer_get_int32(data, &ind) / 1000.0;
		detect_low_duty = (float)buffer_get_int32(data, &ind) / 1000.0;

		chEvtSignal(detect_tp, (eventmask_t) 1);
		break;

	case COMM_REBOOT:
		// Lock the system and enter an infinite loop. The watchdog will reboot.
		__disable_irq();
		for(;;){};
		break;

	case COMM_ALIVE:
		timeout_reset();
		break;

	case COMM_GET_DECODED_PPM:
		ind = 0;
		send_buffer[ind++] = COMM_GET_DECODED_PPM;
		buffer_append_int32(send_buffer, (int32_t)(servodec_get_servo(0) * 1000000.0), &ind);
		send_packet(send_buffer, ind);
		break;

	case COMM_GET_DECODED_CHUK:
		ind = 0;
		send_buffer[ind++] = COMM_GET_DECODED_CHUK;
		buffer_append_int32(send_buffer, (int32_t)(app_nunchuk_get_decoded_chuk() * 1000000.0), &ind);
		send_packet(send_buffer, ind);
		break;

	default:
		break;
	}
}

void commands_printf(char* format, ...) {
	va_list arg;
	va_start (arg, format);
	int len;
	static char print_buffer[255];

	print_buffer[0] = COMM_PRINT;
	len = vsnprintf(print_buffer+1, 254, format, arg);
	va_end (arg);

	if(len>0) {
		send_packet((unsigned char*)print_buffer, (len<254)? len+1: 255);
	}
}

void commands_send_samples(uint8_t *data, int len) {
	uint8_t buffer[len + 1];
	int index = 0;

	buffer[index++] = COMM_SAMPLE_PRINT;

	for (int i = 0;i < len;i++) {
		buffer[index++] = data[i];
	}

	send_packet(buffer, index);
}

void commands_send_rotor_pos(float rotor_pos) {
	uint8_t buffer[5];
	int32_t index = 0;

	buffer[index++] = COMM_ROTOR_POSITION;
	buffer_append_int32(buffer, (int32_t)(rotor_pos * 100000.0), &index);

	send_packet(buffer, index);
}

void commands_send_experiment_samples(float *samples, int len) {
	if ((len * 4 + 1) > 256) {
		return;
	}

	uint8_t buffer[len * 4 + 1];
	int32_t index = 0;

	buffer[index++] = COMM_EXPERIMENT_SAMPLE;

	for (int i = 0;i < len;i++) {
		buffer_append_int32(buffer, (int32_t)(samples[i] * 10000.0), &index);
	}

	send_packet(buffer, index);
}

static msg_t detect_thread(void *arg) {
	(void)arg;

	chRegSetThreadName("Detect");

	detect_tp = chThdSelf();

	for(;;) {
		chEvtWaitAny((eventmask_t) 1);

		if (!conf_general_detect_motor_param(detect_current, detect_min_rpm,
				detect_low_duty, &detect_cycle_int_limit, &detect_coupling_k)) {
			detect_cycle_int_limit = 0.0;
			detect_coupling_k = 0.0;
		}

		int32_t ind = 0;
		send_buffer[ind++] = COMM_DETECT_MOTOR_PARAM;
		buffer_append_int32(send_buffer, (int32_t)(detect_cycle_int_limit * 1000.0), &ind);
		buffer_append_int32(send_buffer, (int32_t)(detect_coupling_k * 1000.0), &ind);
		send_packet(send_buffer, ind);
	}

	return 0;
}
