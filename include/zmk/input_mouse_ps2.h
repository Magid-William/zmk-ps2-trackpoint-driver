/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

int zmk_mouse_ps2_settings_log();
int zmk_mouse_ps2_settings_reset();

/* Exp81: power-policy hooks - stop/restart decoding around a trackpoint rail
 * cycle. Both clear the packet assembler before touching the PS/2 callback. */
int zmk_mouse_ps2_power_suspend(const struct device *dev);
int zmk_mouse_ps2_power_resume(const struct device *dev);

int zmk_mouse_ps2_tp_sensitivity_change(int amount);
int zmk_mouse_ps2_tp_neg_inertia_change(int amount);
int zmk_mouse_ps2_tp_value6_upper_plateau_speed_change(int amount);
int zmk_mouse_ps2_tp_pts_threshold_change(int amount);
