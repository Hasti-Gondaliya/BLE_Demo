/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOSConfig.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "led.h"
#include "temp.h"
#include "gatt_svr.h"

#define TAG "BLE_PRPHRL"
#define DEVICE_NAME Nimble_ble_PRPHL
/* BLE Variables */
static uint16_t conn_handle;
static const char *DEVICE_NAME = "Nimble_ble_PRPH";

extern uint16_t temp_handle; // Characteristic handle
static uint16_t conn_handle; // Connection handle
static bool device_connected = false; // Connection status
static uint8_t notify_enabled = 0; // Notification status

static int ble_prphl_gap_event(struct ble_gap_event *event, void *arg);

static void notify_temperature()
{
	if (device_connected)
	{
		int rc;
		float temp_value = read_temperature();
		uint8_t temp = (uint8_t)temp_value;
		ESP_LOGI(TAG, "=------>Reading %d\n", temp);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&temp, sizeof(temp)); // Create an mbuf for the characteristic value

		if (notify_enabled == 1)
		{
			// Send notification
			rc = ble_gattc_notify_custom(conn_handle, temp_handle, om);
			assert(rc == 0);
		}
		else if (notify_enabled == 2)
		{
			// Send indication
			rc = ble_gattc_indicate_custom(conn_handle, temp_handle, om);
			assert(rc == 0);
		}
		ESP_LOGI(TAG, "Temperature: %d C", temp); // Print temperature to console

		vTaskDelay(5000 / portTICK_PERIOD_MS);     // Wait 5 seconds
	}
}

static int ble_prphl_advertise()
{
	int rc;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    /*
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info)
     *     o Advertising tx power
     *     o Device name
     */
    memset(&fields, 0, sizeof(fields));

    /*
     * Advertise two flags:
     *      o Discoverability in forthcoming advertisement (general)
     *      o BLE-only (BR/EDR unsupported)
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /*
     * Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(BLE_SERVICE_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return rc;
    }

    /* Begin advertising */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_prphl_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return rc;
    }
    return 0;
}

static int ble_prphl_gap_event(struct ble_gap_event *event, void *arg)
{
	switch (event->type)
	{
	case BLE_GAP_EVENT_CONNECT:
		// A new connection was established or a connection attempt failed
		ESP_LOGI(TAG, "connection %s; status=%d ",
				 event->connect.status == 0 ? "established" : "failed",
				 event->connect.status);
		if (event->connect.status == 0)
		{
			conn_handle = event->connect.conn_handle;
			device_connected = true;
		}
		break;

	case BLE_GAP_EVENT_DISCONNECT:
		// Connection terminated
		ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
		conn_handle = BLE_HS_CONN_HANDLE_NONE;
		device_connected = false;
		notify_enabled = 0;
		// Restart advertising
		ble_prphl_advertise();
		break;

	case BLE_GAP_EVENT_ADV_COMPLETE:
		// Advertising terminated
		ESP_LOGI(TAG, "adv complete; reason=%d ", event->adv_complete.reason);
		// Restart advertising
		ble_prphl_advertise();
		break;

	case BLE_GAP_EVENT_SUBSCRIBE:
		// GATT subscribe event
		ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
					  "reason=%d prevn=%d curn=%d previ=%d curi=%d",
				 event->subscribe.conn_handle,
				 event->subscribe.attr_handle,
				 event->subscribe.reason,
				 event->subscribe.prev_notify,
				 event->subscribe.cur_notify,
				 event->subscribe.prev_indicate,
				 event->subscribe.cur_indicate);
		if (event->subscribe.attr_handle == temp_handle)
		{
			if (event->subscribe.cur_notify)
			{
				ESP_LOGI(TAG, "---> Notify enable");
				notify_enabled = 1;
				notify_temperature();
			}
			else if (event->subscribe.cur_indicate)
			{
				ESP_LOGI(TAG, "-----> Indicate enable");
				notify_enabled = 2;
			}
			else
			{
				ESP_LOGI(TAG, "----> Notify/indicate disable");
				notify_enabled = 0;
			}
		}
		break;

	case BLE_GAP_EVENT_NOTIFY_TX:
		// GATT notification/indication event
		ESP_LOGI(TAG, "notify event; conn_handle=%d attr_handle=%d "
					  "status=%d indication=%d",
				 event->notify_tx.conn_handle,
				 event->notify_tx.attr_handle,
				 event->notify_tx.status,
				 event->notify_tx.indication);
		break;

	default:
		break;
	}
	return 0;
}

static void ble_prphl_on_sync(void)
{
    int rc;

    // Set device name
	rc = ble_svc_gap_device_name_set(DEVICE_NAME);
	assert(rc == 0);

    /* Begin advertising */
    rc = ble_prphl_advertise();
    assert(rc == 0);
}

static void ble_prphl_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

void ble_temp_prph_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

void app_main(void)
{
	int rc;

	/* Initialize NVS — it is used to store PHY calibration data */
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	/* Initialize the host stack */
	ret = nimble_port_init();
	if (ret != ESP_OK) {
		MODLOG_DFLT(ERROR, "Failed to init nimble %d \n", ret);
		return;
	}

	 /* Initialize the NimBLE host configuration */
	ble_hs_cfg.sync_cb = ble_prphl_on_sync;
	ble_hs_cfg.reset_cb = ble_prphl_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	/* Configure the peripheral according to the LED type */
	configure_led();

	rc = gatt_svr_init();
	assert(rc == 0);

	/* Set the default device name */
	rc = temp_sensor_init();
	assert(rc == 0);


	nimble_port_freertos_init(ble_temp_prph_host_task);

//	while(1)
//	{
////		if (device_connected)
////		{
////			float temp = read_temperature(); // Read temperature in Celsius
////			char temp_str[10];              // Buffer to store temperature as string
////			sprintf(temp_str, "%.2f", temp);
////			ESP_LOGI(TAG, "---> Temperature read %s\n", temp_str);
////            vTaskDelay(5000 / portTICK_PERIOD_MS);     // Wait 5 seconds
////		}
//	}
}
