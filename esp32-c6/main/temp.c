/*
 * temp.c
 *
 *  Created on: 25-Jan-2024
 *      Author: sahil
 */
#include "host/ble_hs.h"
#include "driver/temperature_sensor.h"
#include "os/endian.h"
#include "temp.h"

#define TAG "Nimble_ble_PRPH-temp"
temperature_sensor_handle_t temp_sensor = NULL;

float read_temp = 0;

static bool temp_sensor_monitor_cbs(temperature_sensor_handle_t tsens, const temperature_sensor_threshold_event_data_t *edata, void *user_data)
{
    ESP_DRAM_LOGI("tsens", "Temperature value is higher or lower than threshold, value is %d\n...\n\n", edata->celsius_value);
    return false;
}

int temp_sensor_init()
{
	ESP_LOGI(TAG, "Install temperature sensor, expected temp ranger range: 10~50 ℃");
	temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
	ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));


	temperature_sensor_event_callbacks_t cbs = {
		.on_threshold = temp_sensor_monitor_cbs,
	};

	temperature_sensor_abs_threshold_config_t threshold_cfg = {
		.high_threshold = 50,
		.low_threshold = -10,
	};
	ESP_ERROR_CHECK(temperature_sensor_set_absolute_threshold(temp_sensor, &threshold_cfg));

	ESP_ERROR_CHECK(temperature_sensor_register_callbacks(temp_sensor, &cbs, NULL));

	ESP_LOGI(TAG, "Enable temperature sensor");
	ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

	ESP_LOGI(TAG, "Read temperature");
	int cnt = 3;
	float read_temp;

	while (cnt--) {
		ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &read_temp));
		ESP_LOGI(TAG, "Temperature value %.02f ℃", read_temp);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	temperature_sensor_disable(temp_sensor);
	return 0;
}

float read_temperature()
{
	ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
	ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &read_temp));
	ESP_LOGI(TAG, "Temperature value %.02f celsius", read_temp);
	temperature_sensor_disable(temp_sensor);
	return read_temp;
}

