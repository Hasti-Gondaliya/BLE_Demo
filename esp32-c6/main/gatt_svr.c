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

static const char *tag = "Nimble_ble_PRPH-gatt-svr";

static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

/* A characteristic that can be subscribed to */
static uint8_t gatt_svr_chr_val;
static uint16_t gatt_svr_chr_val_handle;
static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                     0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);


/* A characteristic that can be subscribed to */
static uint16_t gatt_svr_chr_temp_val_handle;

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
		/* Service: environmental sensing */
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(BLE_UUID_ENVIRONMENTAL_SENSING_SERVICE),
		.characteristics = (struct ble_gatt_chr_def[])
		{ {
				/* Characteristic: Heart-rate measurement */
				.uuid = BLE_UUID16_DECLARE(BLE_UUID_TEMPERATURE_CHAR),
				.access_cb = gatt_svr_chr_access,
				.flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
				.val_handle = &gatt_svr_chr_temp_val_handle,
			}, {
					0, /* No more characteristics in this service */
			},
    	}
    },

	{
		/* Service: Custom service to write char */
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &gatt_svr_svc_uuid.u,
		.characteristics = (struct ble_gatt_chr_def[])
		{ {
				/*** This characteristic can be subscribed to by writing 0x00 and 0x01 to the CCCD ***/
				.uuid = &gatt_svr_chr_uuid.u,
				.access_cb = gatt_svr_chr_access,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
				.val_handle = &gatt_svr_chr_val_handle,
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
    float tsens_value;
	struct os_mbuf *om;
	static uint8_t gatt_svr_chr_temp_val;

	switch (ctxt->op) {
	case BLE_GATT_ACCESS_OP_READ_CHR:

		ESP_LOGI(tag, "Characteristic read; conn_handle=%d attr_handle=%d",
				                        conn_handle, attr_handle);

		if (attr_handle == gatt_svr_chr_temp_val_handle) {
			temperature_sensor_enable(temp_sensor);
			ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));

			gatt_svr_chr_temp_val = (uint8_t)tsens_value;
			rc = os_mbuf_append(ctxt->om, &gatt_svr_chr_temp_val, sizeof(gatt_svr_chr_temp_val));
			om = ble_hs_mbuf_from_flat(&gatt_svr_chr_temp_val, sizeof(gatt_svr_chr_temp_val));
			rc = ble_gatts_notify_custom(conn_handle, gatt_svr_chr_temp_val_handle, om);

			temperature_sensor_disable(temp_sensor);
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}
		if(attr_handle == gatt_svr_chr_val_handle) {
			rc = os_mbuf_append(ctxt->om,
								&gatt_svr_chr_val,
								sizeof(gatt_svr_chr_val));
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}

		goto unknown;

	case BLE_GATT_ACCESS_OP_WRITE_CHR:

		ESP_LOGI(tag, "Characteristic write; conn_handle=%d attr_handle=%d",
		                        conn_handle, attr_handle);

		if (attr_handle == gatt_svr_chr_val_handle) {
			rc = gatt_svr_write(ctxt->om,
								sizeof(gatt_svr_chr_val),
								sizeof(gatt_svr_chr_val),
								&gatt_svr_chr_val, NULL);

			ble_gatts_chr_updated(attr_handle);
			ESP_LOGI(tag, "Notification/Indication scheduled for "
						"all subscribed peers.\n");
			return rc;
		}
		goto unknown;
	default:
		goto unknown;
	}

unknown:
	/* Unknown characteristic/descriptor;
	 * The NimBLE host should not have called this function;
	 */
	assert(0);
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



