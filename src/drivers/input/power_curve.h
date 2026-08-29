/*
 * SPDX-License-Identifier: MIT
 *
 * PowerCurve - RawAccel-style "Power" velocity curve for the direct-PS/2
 * trackpoint.
 *
 *   v     = |(x,y)| = sqrt(x*x + y*y)              (whole mode, L2 magnitude)
 *   f(v)  = start + (rate * v)^exponent            (Power sensitivity function)
 *   out   = (x * sens, y * sens) * f(v)            (applied to the whole vector)
 *
 * The curve uses a 64-entry Q8.8 gain LUT rebuilt with float pow() when a
 * parameter changes, an integer isqrt for the magnitude (no libm in the
 * per-sample path), one fixed-point multiply with fractional-remainder
 * accumulation, and an int8 clamp (OUT_MAX 127) so HID reports stay int8.
 *
 * Identity defaults (rate = 0, exp = 256, start = 256) give f(v) = 1.0, so
 * the gain is exactly `sens` and a node that sets nothing behaves like the
 * raw stream.
 */

#ifndef ZMK_DRIVERS_INPUT_POWER_CURVE_H
#define ZMK_DRIVERS_INPUT_POWER_CURVE_H

#include <stdbool.h>
#include <stdint.h>

struct power_curve {
	uint8_t sens;	      /* Q8.8                                   */
	uint16_t rate;	      /* Q8.8                                   */
	uint16_t exp;	      /* Q8.8                                   */
	uint16_t start;	      /* Q8.8                                   */
	uint16_t lut[64];     /* Q8.8 gain for each speed bin           */
	int16_t rem_x;	      /* fractional-remainder accumulator       */
	int16_t rem_y;	      /* fractional-remainder accumulator       */
	bool dirty;	      /* LUT needs rebuild (param write)        */
};

/* Reset to identity defaults and build the (identity) LUT. */
void power_curve_init(struct power_curve *c);

/* Store all four params (Q8.8) and mark the LUT dirty. */
void power_curve_set_param(struct power_curve *c, int sens, int rate, int exp, int start);

/* Rebuild the LUT if any param changed since the last call (no-op when clean). */
void power_curve_update(struct power_curve *c);

/* Apply the curve to one input sample (int16 in/out, clamped to +/-127). */
void power_curve_apply(struct power_curve *c, int16_t x, int16_t y, int16_t *out_x,
		       int16_t *out_y);

#endif /* ZMK_DRIVERS_INPUT_POWER_CURVE_H */