/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic mesh light sample
 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include "smp_bt.h"

/* Hold SW1 for this long to trigger a factory reset */
#define FACTORY_RESET_HOLD_MS 4000
#define FACTORY_RESET_BLINK_MS 200

static struct k_work_delayable factory_reset_work;
static struct k_work_delayable factory_reset_blink_work;
static int factory_reset_blink_count;

static void factory_reset_blink_handler(struct k_work *work)
{
	factory_reset_blink_count--;
	dk_set_leds(factory_reset_blink_count & 1 ? DK_ALL_LEDS_MSK : DK_NO_LEDS_MSK);
	if (factory_reset_blink_count > 0) {
		k_work_reschedule(&factory_reset_blink_work, K_MSEC(FACTORY_RESET_BLINK_MS));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
		bt_mesh_reset();
		printk("Factory reset!\n");
	}
}

static void factory_reset_handler(struct k_work *work)
{
	/* Blink 3 times (6 toggles) to confirm, then reset */
	factory_reset_blink_count = 6;
	k_work_reschedule(&factory_reset_blink_work, K_NO_WAIT);
}

static void button_handler_cb(uint32_t button_state, uint32_t has_changed)
{
	if (!(has_changed & BIT(0))) {
		return;
	}

	if (button_state & BIT(0)) {
		/* Button pressed: start factory reset countdown */
		k_work_reschedule(&factory_reset_work, K_MSEC(FACTORY_RESET_HOLD_MS));
	} else {
		/* Button released: if countdown still pending it was a short press */
		if (k_work_delayable_is_pending(&factory_reset_work)) {
			k_work_cancel_delayable(&factory_reset_work);
			model_handler_led_toggle();
		}
	}
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = dk_leds_init();
	if (err) {
		printk("Initializing LEDs failed (err %d)\n", err);
		return;
	}

	k_work_init_delayable(&factory_reset_work, factory_reset_handler);
	k_work_init_delayable(&factory_reset_blink_work, factory_reset_blink_handler);

	err = dk_buttons_init(button_handler_cb);
	if (err) {
		printk("Initializing buttons failed (err %d)\n", err);
		return;
	}

	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* This will be a no-op if settings_load() loaded provisioning info */
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");

	if (IS_ENABLED(CONFIG_MCUMGR_TRANSPORT_BT)) {
		err = smp_dfu_init();
		if (err) {
			printk("Unable to initialize DFU (err %d)\n", err);
		}
	}
}

int main(void)
{
	int err;

	printk("Initializing... (built " __DATE__ " " __TIME__ ")\n");
	bt_set_name("RPR Thingy 1 (Build " __DATE__ " " __TIME__ ")");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	return 0;
}
