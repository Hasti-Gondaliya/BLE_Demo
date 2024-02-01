/*
 * gatt_svr.h
 *
 *  Created on: 25-Jan-2024
 *      Author: Hasti
 */

#ifndef MAIN_GATT_SVR_H_
#define MAIN_GATT_SVR_H_

#include "nimble/ble.h"

#define BLE_UUID_ENVIRONMENTAL_SENSING_SERVICE        0x181A // environmental sensing service uuid
#define BLE_UUID_TEMPERATURE_CHAR                     0x2A6E     /**< temperature characteristic UUID. */

int gatt_svr_init(void);

#endif /* MAIN_GATT_SVR_H_ */
