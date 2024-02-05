/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/settings/settings.h>

#include <zephyr/kernel.h>

#define STACKSIZE 1024
#define PRIORITY 7

#define RUN_STATUS_LED             DK_LED1
#define CENTRAL_CON_STATUS_LED	   DK_LED2
#define PERIPHERAL_CONN_STATUS_LED DK_LED3

#define RUN_LED_BLINK_INTERVAL 1000

#define HRS_QUEUE_SIZE 16


#define CUSTOM_SERVICE_UUID_VAL 0x1234 // Custom Service
#define CUSTOM_LED_CHAR_UUID_VAL 0x5678 // Custom LED Characteristic

#define CUSTOM_SERVICE_UUID \
	BT_UUID_DECLARE_16(CUSTOM_SERVICE_UUID_VAL)

#define CUSTOM_LED_CHAR_UUID \
	BT_UUID_DECLARE_16(CUSTOM_LED_CHAR_UUID_VAL)
	
static struct bt_conn *central_conn;
static struct bt_uuid_16 discover_uuid[2] = {BT_UUID_INIT_16(0)};
static struct bt_uuid_16 read_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params[2];
static struct bt_gatt_subscribe_params subscribe_params[2];
static struct bt_gatt_read_params read_params;
static struct bt_gatt_write_params write_params;

static void led_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void ess_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

static void read_temp(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset);
static void write_led(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 		uint16_t len, uint16_t offset, uint8_t flags);
static void read_led(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset);

static uint8_t temp_val;
static uint8_t led_status;

BT_GATT_SERVICE_DEFINE(my_ess_svc, 
        BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
        BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE, 
                    BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_temp, NULL, &temp_val),
		BT_GATT_CCC(ess_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

BT_GATT_SERVICE_DEFINE(my_custom_led_svc, 
        BT_GATT_PRIMARY_SERVICE(CUSTOM_SERVICE_UUID),
        BT_GATT_CHARACTERISTIC(CUSTOM_LED_CHAR_UUID, 
                    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE,
                    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_led, write_led, &led_status),
		BT_GATT_CCC(led_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static void ess_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				       uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	if(notif_enabled)
	{
		bt_gatt_notify(NULL, attr, &temp_val, sizeof(temp_val));
	}
}

static void led_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				       uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	if(notif_enabled)
	{
		bt_gatt_notify(NULL, attr, &led_status, sizeof(led_status));
	}

	printk("Notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		(CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		(CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)), /* Heart Rate Service */
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME)
};

// K_MSGQ_DEFINE(hrs_queue, sizeof(struct bt_hrs_client_measurement), HRS_QUEUE_SIZE, 4);

// static struct bt_hrs_client hrs_c;
static struct bt_conn *central_conn;

static uint8_t notify_led_status(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data) {
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	const uint16_t *dtemp = data;
	led_status = dtemp[0];

	printk("[NOTIFICATION] data %d length %u\n", led_status, length);
	bt_gatt_notify(NULL, &(my_custom_led_svc.attrs[1]), &led_status, sizeof(led_status));

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t notify_temp(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data) {
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	const uint16_t *dtemp = data;
	temp_val = dtemp[0];

	printk("[NOTIFICATION] data %d length %u\n", temp_val, length);
	bt_gatt_notify(NULL, &(my_ess_svc.attrs[1]), &temp_val, sizeof(temp_val));

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t read_func(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params,
				    const void *data, uint16_t length)
{
	if (!data) {
		return BT_GATT_ITER_STOP;
	}

	const uint16_t *dtemp = data;
	temp_val = dtemp[0];
	printk("[READ DATA] %d and %d\n", temp_val, params->single.handle);

	return BT_GATT_ITER_CONTINUE;

}

static void read_temp(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	int rc;
	memcpy(&read_uuid, BT_UUID_TEMPERATURE, sizeof(read_uuid));
	read_params.func = read_func;
	read_params.handle_count = 0;
	read_params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	read_params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	read_params.by_uuid.uuid = &read_uuid.uuid;
	rc = bt_gatt_read(central_conn, &read_params);
	
	bt_gatt_attr_read(central_conn, attr, buf, len, offset, &temp_val,
				sizeof(temp_val));
}

static void write_func(struct bt_conn *conn, uint8_t err,
				     struct bt_gatt_write_params *params)
{

}

static void write_led(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags)
{
	int err;
	uint8_t val[len];

	memcpy(val, buf, len);
	uint16_t handle = bt_gatt_attr_get_handle(bt_gatt_find_by_uuid(NULL, 1, CUSTOM_LED_CHAR_UUID));

	write_params.func = write_func;
	write_params.handle = handle-2;
	write_params.offset = 0;
	write_params.data = val;
	write_params.length = (uint16_t)sizeof(val);

	err = bt_gatt_write(central_conn, &write_params);
	if(err)
	{
		printk("write error!\n");
	}
	else
	{
		printk("write request succeeded\n");
	}
}

static void read_led(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	bt_gatt_attr_read(central_conn, attr, buf, len, offset, &led_status,
				 sizeof(led_status));
}

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		printk("Discover complete\n");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (!bt_uuid_cmp(discover_params[0].uuid, BT_UUID_ESS)) {
		memcpy(&discover_uuid[0], BT_UUID_TEMPERATURE, sizeof(discover_uuid[0]));
		discover_params[0].uuid = &discover_uuid[0].uuid;
		discover_params[0].start_handle = attr->handle + 1;
		discover_params[0].type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params[0]);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else if (!bt_uuid_cmp(discover_params[0].uuid,
				BT_UUID_TEMPERATURE)) {
		memcpy(&discover_uuid[0], BT_UUID_GATT_CCC, sizeof(discover_uuid[0]));
		discover_params[0].uuid = &discover_uuid[0].uuid;
		discover_params[0].start_handle = attr->handle + 2;
		discover_params[0].type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params[0].value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params[0]);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else {
		subscribe_params[0].notify = notify_temp;
		subscribe_params[0].value = BT_GATT_CCC_NOTIFY;
		subscribe_params[0].ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params[0]);
		if (err && err != -EALREADY) {
			printk("Subscribe failed (err %d)\n", err);
		} else {
			printk("[SUBSCRIBED]\n");
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t discover_func1(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		printk("Discover complete\n");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (!bt_uuid_cmp(discover_params[1].uuid, CUSTOM_SERVICE_UUID)) {
		memcpy(&discover_uuid[1], CUSTOM_LED_CHAR_UUID, sizeof(discover_uuid[1]));
		discover_params[1].uuid = &discover_uuid[1].uuid;
		discover_params[1].start_handle = attr->handle + 1;
		discover_params[1].type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params[1]);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else if (!bt_uuid_cmp(discover_params[1].uuid,
				CUSTOM_LED_CHAR_UUID)) {
		memcpy(&discover_uuid[1], BT_UUID_GATT_CCC, sizeof(discover_uuid[1]));
		discover_params[1].uuid = &discover_uuid[1].uuid;
		discover_params[1].start_handle = attr->handle + 2;
		discover_params[1].type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params[1].value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params[1]);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else {
		subscribe_params[1].notify = notify_led_status;
		subscribe_params[1].value = BT_GATT_CCC_NOTIFY;
		subscribe_params[1].ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params[1]);
		if (err && err != -EALREADY) {
			printk("Subscribe failed (err %d)\n", err);
		} else {
			printk("SUBSCRIBED!!\n");
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

static void gatt_discover(struct bt_conn *conn)
{
	int err;
	memcpy(&discover_uuid[0], BT_UUID_ESS, sizeof(discover_uuid[0]));
	discover_params[0].uuid = &discover_uuid[0].uuid;
	discover_params[0].func = discover_func;
	discover_params[0].start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params[0].end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params[0].type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(central_conn, &discover_params[0]);
	if (err) {
		printk("Discover failed(err %d)\n", err);
		return;
	}

	memcpy(&discover_uuid[1], CUSTOM_SERVICE_UUID, sizeof(discover_uuid[1]));
	discover_params[1].uuid = &discover_uuid[1].uuid;
	discover_params[1].func = discover_func1;
	discover_params[1].start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params[1].end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params[1].type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(central_conn, &discover_params[1]);
	if (err) {
		printk("Discover failed(err %d)\n", err);
		return;
	}
}

static int scan_start(void)
{
	int err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);

	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
	}

	return err;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	// int err;
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);

		if (conn == central_conn) {
			bt_conn_unref(central_conn);
			central_conn = NULL;

			scan_start();
		}

		return;
	}

	printk("Connected: %s\n", addr);

	bt_conn_get_info(conn, &info);

	if (info.role == BT_CONN_ROLE_CENTRAL) {
		dk_set_led_on(CENTRAL_CON_STATUS_LED);
			gatt_discover(conn);
	} else {
		dk_set_led_on(PERIPHERAL_CONN_STATUS_LED);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason %u)\n", addr, reason);

	if (conn == central_conn) {
		dk_set_led_off(CENTRAL_CON_STATUS_LED);

		bt_conn_unref(central_conn);
		central_conn = NULL;

		scan_start();
	} else {
		dk_set_led_off(PERIPHERAL_CONN_STATUS_LED);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Filters matched. Address: %s connectable: %d\n",
		   addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	printk("Connecting failed\n");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	central_conn = bt_conn_ref(conn);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
		scan_connecting_error, scan_connecting);

static void scan_init(void)
{
	int err;

	struct bt_scan_init_param param = {
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT,
		.connect_if_match = 1
	};

	bt_scan_init(&param);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_ESS);
	if (err) {
		printk("Scanning filters cannot be set (err %d)\n", err);
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("Filters cannot be turned on (err %d)\n", err);
	}
}

int main(void)
{
	int err;
	int blink_status = 0;

	printk("Starting Bluetooth Central and Peripheral Heart Rate relay example\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		return 0;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	scan_init();

	err = scan_start();
	if (err) {
		return 0;
	}

	printk("Scanning started\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising started\n");

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
