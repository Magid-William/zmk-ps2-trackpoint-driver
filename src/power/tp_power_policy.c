/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Exp81: trackpoint rail power policy.
 *
 * Powers the trackpoint only while the keyboard is in use, driven by ZMK's
 * own activity state (the same signal that feeds CONFIG_ZMK_IDLE_SLEEP_TIMEOUT
 * and is raised by any key, encoder or pointer event):
 *
 *   ACTIVE -> rail ON, PS/2 callback armed after ZMK_PS2_TP_POWER_BOOT_MUTE_MS
 *   IDLE   -> rail OFF, callback disarmed, CLK/DAT disconnected
 *   SLEEP  -> same as IDLE; the pin configuration survives into System OFF,
 *             so the PS/2 pull-ups cannot leak into the unpowered module
 *
 * USB overrides the idle cut: VBUS has no battery budget, and ZMK already
 * refuses to deep sleep on USB for the same reason. The rail is switched on
 * as soon as VBUS appears (plugging in while idle emits no activity event),
 * stays on for as long as USB is present, and falls back to the activity
 * state on unplug.
 *
 * Consequence, by design: once the rail is off the trackpoint is blind, so
 * the next key press (not the nub) brings it back.
 *
 * Why the pins are disconnected: PS/2 needs pull-ups on CLK/DAT
 * (CONFIG_PS2_GPIO_INTERNAL_PULLUP), and with the rail off those pull-ups
 * push current through the module's ESD diodes into its unpowered VCC rail.
 * That leak was measured in the field as ~4%/night of a small LiPo - about
 * 40x the nRF System OFF current.
 *
 * Why the rail GPIO is driven directly instead of ext_power_disable():
 * ext_power_generic_disable() schedules a settings_save_one() on every call,
 * so a 60 s idle cycle would churn the settings partition twice a minute and
 * can persist "ext_power = off" across a reboot. The ext_power driver keeps
 * its status at "on"; do not use the &ext_power behavior on these builds.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/input_mouse_ps2.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TP_INPUT_NODE DT_INST(0, zmk_input_mouse_ps2)
#define TP_PS2_NODE DT_INST(0, gpio_ps2)
#define TP_EXT_NODE DT_INST(0, zmk_ext_power_generic)

#if DT_NODE_HAS_STATUS(TP_INPUT_NODE, okay) && DT_NODE_HAS_STATUS(TP_PS2_NODE, okay) &&            \
    DT_NODE_HAS_STATUS(TP_EXT_NODE, okay)

static const struct device *const tp_input = DEVICE_DT_GET(TP_INPUT_NODE);
static const struct gpio_dt_spec tp_clk = GPIO_DT_SPEC_GET(TP_PS2_NODE, scl_gpios);
static const struct gpio_dt_spec tp_dat = GPIO_DT_SPEC_GET(TP_PS2_NODE, sda_gpios);
static const struct gpio_dt_spec tp_rail = GPIO_DT_SPEC_GET_BY_IDX(TP_EXT_NODE, control_gpios, 0);

BUILD_ASSERT(DT_PROP_LEN(TP_EXT_NODE, control_gpios) == 1,
             "TP power policy drives exactly one EXT_POWER control pin");

static bool tp_powered;
static struct k_work_delayable tp_boot_mute_work;

static gpio_flags_t tp_line_flags(void) {
    gpio_flags_t flags = GPIO_INPUT;
#if IS_ENABLED(CONFIG_PS2_GPIO_INTERNAL_PULLUP)
    flags |= GPIO_PULL_UP;
#endif
    return flags;
}

static void tp_rail_off(void) {
    /* Cut the rail first, then release the lines: nothing can feed the
     * unpowered module in between. */
    (void)gpio_pin_set_dt(&tp_rail, 0);
    (void)gpio_pin_configure_dt(&tp_clk, GPIO_DISCONNECTED);
    (void)gpio_pin_configure_dt(&tp_dat, GPIO_DISCONNECTED);
}

static void tp_rail_on(void) {
    (void)gpio_pin_set_dt(&tp_rail, 1);
    (void)gpio_pin_configure_dt(&tp_clk, tp_line_flags());
    (void)gpio_pin_configure_dt(&tp_dat, tp_line_flags());
}

static void tp_power_off(void) {
    if (!tp_powered) {
        return;
    }
    tp_powered = false;

    k_work_cancel_delayable(&tp_boot_mute_work);

    /* Disarm the callback and clear the 3-byte assembler before the module
     * goes dark, so nothing stale survives the cycle. */
    (void)zmk_mouse_ps2_power_suspend(tp_input);
    tp_rail_off();

    LOG_INF("TP power: off (rail low, CLK/DAT disconnected)");
}

static void tp_boot_mute_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!tp_powered) {
        return;
    }

    int err = zmk_mouse_ps2_power_resume(tp_input);
    if (err) {
        LOG_ERR("TP power: could not arm the PS/2 callback (%d)", err);
        return;
    }

    LOG_INF("TP power: armed after %d ms mute", CONFIG_ZMK_PS2_TP_POWER_BOOT_MUTE_MS);
}

static void tp_power_on(void) {
    if (tp_powered) {
        return;
    }
    tp_powered = true;

    tp_rail_on();
    k_work_reschedule(&tp_boot_mute_work, K_MSEC(CONFIG_ZMK_PS2_TP_POWER_BOOT_MUTE_MS));

    LOG_INF("TP power: on (mute %d ms)", CONFIG_ZMK_PS2_TP_POWER_BOOT_MUTE_MS);
}

static bool tp_usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static int tp_power_activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return -ENOTSUP;
    }

    /* USB has no battery budget: never cut the rail while VBUS is present. */
    if (ev->state == ZMK_ACTIVITY_ACTIVE || tp_usb_powered()) {
        tp_power_on();
    } else {
        tp_power_off();
    }

    return 0;
}

ZMK_LISTENER(tp_power_policy, tp_power_activity_listener);
ZMK_SUBSCRIPTION(tp_power_policy, zmk_activity_state_changed);

#if IS_ENABLED(CONFIG_ZMK_USB)
static int tp_power_usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);

    if (ev == NULL) {
        return -ENOTSUP;
    }

    if (ev->conn_state != ZMK_USB_CONN_NONE) {
        /* Plugging in while idle raises no activity event: arm it here. */
        tp_power_on();
    } else if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        /* Unplugged and already idle: do not wait for the next timer tick. */
        tp_power_off();
    }

    return 0;
}

ZMK_LISTENER(tp_power_usb, tp_power_usb_listener);
ZMK_SUBSCRIPTION(tp_power_usb, zmk_usb_conn_state_changed);
#endif

static int tp_power_policy_init(void) {
    if (!gpio_is_ready_dt(&tp_rail) || !device_is_ready(tp_input)) {
        LOG_ERR("TP power: EXT_POWER or input device not ready");
        return -ENODEV;
    }

    k_work_init_delayable(&tp_boot_mute_work, tp_boot_mute_handler);

    /* Boot is an "active" state, and the input driver arms its own callback.
     * Force the rail on: a settings restore of ext_power must not leave the
     * trackpoint dead until the first idle->active edge. */
    tp_powered = true;
    (void)gpio_pin_set_dt(&tp_rail, 1);

    return 0;
}

SYS_INIT(tp_power_policy_init, APPLICATION, 91);

#endif /* required devicetree nodes present */
