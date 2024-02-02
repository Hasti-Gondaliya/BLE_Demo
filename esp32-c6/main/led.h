/*
 * led.h
 *
 *  Created on: 25-Jan-2024
 *      Author: Hasti
 */

#ifndef MAIN_LED_H_
#define MAIN_LED_H_

#include "led_strip.h"

#define CUSTOM_SERVICE_UUID 0x1234 // Custom Service
#define CUSTOM_LED_CHAR_UUID 0x5678 // Custom LED Characteristic

void configure_led(void);
void blink_led(uint8_t set_led_status);

#endif /* MAIN_LED_H_ */
