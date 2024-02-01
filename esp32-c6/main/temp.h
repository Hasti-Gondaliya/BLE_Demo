/*
 * temp.h
 *
 *  Created on: 25-Jan-2024
 *      Author: sahil
 */

#ifndef MAIN_TEMP_H_
#define MAIN_TEMP_H_

#include "driver/temperature_sensor.h"

extern temperature_sensor_handle_t temp_sensor;

int temp_sensor_init(void);

#endif /* MAIN_TEMP_H_ */
