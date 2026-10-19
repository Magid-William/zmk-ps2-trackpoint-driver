/*
 * Module-private extension for the gpio-ps2 backend.
 *
 * Zephyr's PS/2 API has no way to tell the high-level device driver that a
 * byte was lost (the upstream resend callback needs a forked zephyr: the
 * pinned v4.1.0+zmk-fixes still exposes the one-argument ps2_config()).
 *
 * Direct-wired trackpoints that reject host commands run with
 * CONFIG_PS2_GPIO_NO_RESEND, so a byte that fails the parity/stop/timing
 * checks is simply discarded. Without this notification the input driver
 * keeps its partial 3-byte packet and the next packet's bytes complete it,
 * producing a bogus movement. Register a callback here to get told exactly
 * when a byte was lost, in order with the delivered bytes.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ps2_gpio_read_error_cb_t)(const struct device *ps2_dev);

/**
 * Register a callback for byte-level read errors on the gpio-ps2 backend.
 *
 * The callback runs on the backend's callback work queue, in order with the
 * byte callbacks: every byte delivered before the error is delivered before
 * this callback, and every byte after it is delivered afterwards.
 *
 * @param ps2_dev gpio-ps2 device
 * @param cb      callback, or NULL to unregister
 *
 * @retval 0 on success
 * @retval -ENODEV if ps2_dev is not the gpio-ps2 device
 */
int ps2_gpio_set_read_error_callback(const struct device *ps2_dev, ps2_gpio_read_error_cb_t cb);

#ifdef __cplusplus
}
#endif
