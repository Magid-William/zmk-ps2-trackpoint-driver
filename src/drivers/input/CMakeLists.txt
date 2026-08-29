# Copyright (c) 2022 The ZMK Contributors
 # SPDX-License-Identifier: MIT

 target_sources_ifdef(CONFIG_ZMK_INPUT_MOUSE_PS2 app PRIVATE input_mouse_ps2.c)

 target_sources_ifdef(CONFIG_ZMK_INPUT_MOUSE_PS2_POWER_CURVE app PRIVATE power_curve.c)