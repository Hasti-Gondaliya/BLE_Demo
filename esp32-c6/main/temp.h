/*
 * temp.h
 *
 *  Created on: 25-Jan-2024
 *      Author: sahil
 */

#ifndef MAIN_TEMP_H_
#define MAIN_TEMP_H_

#include "driver/temperature_sensor.h"

#define BLE_SERVICE_UUID 0x181A // Environmental Sensing Service
#define BLE_TEMP_CHAR_UUID 0x2A6E // Temperature Characteristic
#define BLE_TEMP_DESC_UUID 0x2902 // Client Characteristic Configuration Descriptor

extern temperature_sensor_handle_t temp_sensor;

int temp_sensor_init(void);
float read_temperature();

#endif /* MAIN_TEMP_H_ */
