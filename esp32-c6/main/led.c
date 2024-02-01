/*
 * led.c
 *
 *  Created on: 25-Jan-2024
 *      Author: Hasti
 */

#ifndef MAIN_LED_C_
#define MAIN_LED_C_

#include "led.h"
#include "esp_log.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO

static led_strip_handle_t led_strip;
static const char *tag = "Nimble_ble_PRPH-led";

void configure_led(void)
{
    ESP_LOGI(tag, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

void blink_led()
{
	while(1)
	{
		led_strip_set_pixel(led_strip, 0, 16, 16, 16);
		/* Refresh the strip to send data */
		led_strip_refresh(led_strip);
	}
    /* If the addressable LED is enabled */
//    if (s_led_state) {
//        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
//        led_strip_set_pixel(led_strip, 0, 16, 16, 16);
//        /* Refresh the strip to send data */
//        led_strip_refresh(led_strip);
//    } else {
//        /* Set all LED off to clear all pixels */
//        led_strip_clear(led_strip);
//    }
}

#endif /* MAIN_LED_C_ */
