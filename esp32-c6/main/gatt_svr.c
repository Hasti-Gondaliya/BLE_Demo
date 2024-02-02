/*
 * gatt_svr.c
 *
 *  Created on: 25-Jan-2024
 *      Author: Hasti
 */
#include <assert.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "driver/temperature_sensor.h"
#include "os/endian.h"

#include "temp.h"
#include "led.h"
#include "gatt_svr.h"

#define TAG "Nimble_ble_PRPH-gatt-svr"

/* A characteristic that can be subscribed to */
uint16_t temp_handle;
uint16_t led_handle;
static uint8_t gatt_svr_chr_val;
static uint8_t notify_enabled = 0; // Notification status

extern uint8_t led_status;

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
	{
		/* Service: environmental sensing */
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(BLE_SERVICE_UUID),
		.characteristics = (struct ble_gatt_chr_def[])
		{ {
				/* Characteristic: Heart-rate measurement */
				.uuid = BLE_UUID16_DECLARE(BLE_TEMP_CHAR_UUID),
				.access_cb = gatt_svr_chr_access,
				.flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
				.val_handle = &temp_handle,
			}, {
					0, /* No more characteristics in this service */
			},
		}
	},

	{
		/* Service: Custom service to write char */
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(CUSTOM_SERVICE_UUID),
		.characteristics = (struct ble_gatt_chr_def[])
		{ {
				/*** This characteristic can be subscribed to by writing 0x00 and 0x01 to the CCCD ***/
				.uuid = BLE_UUID16_DECLARE(CUSTOM_LED_CHAR_UUID),
				.access_cb = gatt_svr_chr_access,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
				.val_handle = &led_handle,
			}, {
					0, /* No more descriptors in this characteristic */
			},
		},
	},
	{
		0, /* No more services. */
	},
};

static int gatt_svr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
               void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	int rc;
//	char temp_str[2];

	ESP_LOGI(TAG, "GATT access event; conn_handle=%d attr_handle=%d "
				  "op=%d",
			 conn_handle, attr_handle, ctxt->op);

	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
	{
		// Read characteristic value
		if(attr_handle == temp_handle)
		{
			float temp_value = read_temperature();
			uint8_t temp = (uint8_t)temp_value;
			rc = os_mbuf_append(ctxt->om, &temp, sizeof(temp));
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}
		if(attr_handle == led_handle) {
			rc = os_mbuf_append(ctxt->om,
								&led_status,
								sizeof(led_status));
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}
	}
	else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
	{
		// Write characteristic value
		ESP_LOGI(TAG, "Write event; data_len=%d data=", ctxt->om->om_len);
		esp_log_buffer_hex(TAG, ctxt->om->om_data, ctxt->om->om_len);
		if(attr_handle == led_handle)
		{
			rc = gatt_svr_write(ctxt->om,
								sizeof(gatt_svr_chr_val),
								sizeof(gatt_svr_chr_val),
								&gatt_svr_chr_val, NULL);
			ble_gatts_chr_updated(attr_handle);
			blink_led(gatt_svr_chr_val);
			ESP_LOGI(TAG, "Notification/Indication scheduled for "
						"all subscribed peers.\n");
		}
		return 0;
	}
	else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC)
	{
		// Read descriptor value
		uint16_t desc_val = htole16(notify_enabled);
		rc = os_mbuf_append(ctxt->om, &desc_val, sizeof(desc_val));
		return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
	}
	else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC)
	{
		// Write descriptor value
		uint16_t desc_val;
		if (ctxt->om->om_len != 2)
		{
			return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
		}
		desc_val = le16toh(ctxt->om->om_data);
		if (desc_val == 0x0001)
		{
			ESP_LOGI(TAG, "Notify enable");
			notify_enabled = 1;
		}
		else if (desc_val == 0x0002)
		{
			ESP_LOGI(TAG, "Indicate enable");
			notify_enabled = 2;
		}
		else if (desc_val == 0x0000)
		{
			ESP_LOGI(TAG, "Notify/indicate disable");
			notify_enabled = 0;
		}
		else
		{
			return BLE_ATT_ERR_UNLIKELY;
		}
		return 0;
	}
	return BLE_ATT_ERR_UNLIKELY;
}

int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}



