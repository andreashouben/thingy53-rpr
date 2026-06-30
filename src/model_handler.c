/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh/rpr_srv.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"

static void lightness_set(struct bt_mesh_lightness_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_lightness_set *set,
			  struct bt_mesh_lightness_status *rsp);

static void lightness_get(struct bt_mesh_lightness_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_lightness_status *rsp);

static const struct bt_mesh_lightness_srv_handlers lightness_handlers = {
	.light_set = lightness_set,
	.light_get = lightness_get,
};

static struct bt_mesh_lightness_srv lightness_srv =
	BT_MESH_LIGHTNESS_SRV_INIT(&lightness_handlers);

static uint16_t current_lightness;

static void lightness_set(struct bt_mesh_lightness_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_lightness_set *set,
			  struct bt_mesh_lightness_status *rsp)
{
	current_lightness = set->lvl;
	dk_set_led(0, current_lightness > 0);

	if (rsp) {
		rsp->current = current_lightness;
		rsp->target  = current_lightness;
		rsp->remaining_time = 0;
	}
}

static void lightness_get(struct bt_mesh_lightness_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_lightness_status *rsp)
{
	rsp->current = current_lightness;
	rsp->target  = current_lightness;
	rsp->remaining_time = 0;
}

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
		BIT(3),
#endif
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
	}
}

static void attention_on(const struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(const struct bt_mesh_model *mod)
{
	/* Will stop rescheduling blink timer */
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(
		1, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_CFG_SRV,
			BT_MESH_MODEL_RPR_SRV,
			BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
			BT_MESH_MODEL_LIGHTNESS_SRV(&lightness_srv)),
		BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	k_work_init_delayable(&attention_blink_work, attention_blink);
	return &comp;
}

void model_handler_led_toggle(void)
{
	uint16_t new_lvl = current_lightness > 0 ? 0 : UINT16_MAX;
	struct bt_mesh_lightness_set set = {
		.lvl = new_lvl,
	};
	struct bt_mesh_lightness_status status;

	lightness_set(&lightness_srv, NULL, &set, &status);
	bt_mesh_lightness_srv_pub(&lightness_srv, NULL, &status);
}
