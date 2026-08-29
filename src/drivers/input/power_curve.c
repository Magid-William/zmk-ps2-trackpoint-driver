/*
 * SPDX-License-Identifier: MIT
 *
 * PowerCurve implementation. All per-sample math is integer; float pow()
 * runs only in power_curve_update(), which is called at init and after any
 * param write.
 */

#include "power_curve.h"

#include <math.h>

#define LUT_SIZE 64
#define CLAMP_MAX 2048 /* Q8.8: max gain 8.0 */
#define OUT_MAX 127

void power_curve_init(struct power_curve *c)
{
	c->sens = 255;
	c->rate = 0;
	c->exp = 256;
	c->start = 256; /* Q8.8 1.0 - safe identity default */
	c->rem_x = 0;
	c->rem_y = 0;
	c->dirty = 1;
	power_curve_update(c);
}

void power_curve_set_param(struct power_curve *c, int sens, int rate, int exp, int start)
{
	c->sens = (uint8_t)sens;
	c->rate = (uint16_t)rate;
	c->exp = (uint16_t)exp;
	c->start = (uint16_t)start;
	c->dirty = 1;
}

void power_curve_update(struct power_curve *c)
{
	if (!c->dirty) {
		return;
	}

	double r = (double)c->rate / 256.0;
	double e = (double)c->exp / 256.0;
	double s = (double)c->start / 256.0;
	double sn = (double)c->sens / 256.0;

	for (uint16_t v = 0; v < LUT_SIZE; v++) {
		double f = s + pow(r * (double)v, e);
		double val = sn * f;
		if (val > (double)CLAMP_MAX / 256.0) {
			val = (double)CLAMP_MAX / 256.0;
		}
		c->lut[v] = (uint16_t)(val * 256.0 + 0.5);
	}

	c->dirty = 0;
}

/* Digit-by-digit integer square root (floor), no libm dependency at runtime. */
static uint16_t isqrt16(uint16_t n)
{
	uint16_t res = 0;
	uint16_t bit = (uint16_t)1 << 14;

	while (bit > n) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (n >= res + bit) {
			n -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return res;
}

void power_curve_apply(struct power_curve *c, int16_t x, int16_t y, int16_t *out_x,
		       int16_t *out_y)
{
	if (c->dirty) {
		power_curve_update(c);
	}

	if (x == 0 && y == 0) {
		*out_x = 0;
		*out_y = 0;
		c->rem_x = 0;
		c->rem_y = 0;
		return;
	}

	/* L2 magnitude (whole mode). PS/2 deltas are int8 so this stays under
	 * 65536 for real data; guard anyway so a wide int16 input can't wrap
	 * the uint16 square-root input. Values above LUT_SIZE saturate to the
	 * top gain bin.
	 */
	uint32_t mag2 = (uint32_t)((int32_t)x * x) + (uint32_t)((int32_t)y * y);
	uint16_t v;
	if (mag2 > UINT16_MAX) {
		v = LUT_SIZE - 1;
	} else {
		v = isqrt16((uint16_t)mag2);
		if (v >= LUT_SIZE) {
			v = LUT_SIZE - 1;
		}
	}
	uint16_t g = c->lut[v];

	int32_t tx = (int32_t)x * g + c->rem_x;
	int32_t ty = (int32_t)y * g + c->rem_y;
	int16_t sx = tx / 256;
	int16_t sy = ty / 256;
	c->rem_x = tx - (int32_t)sx * 256;
	c->rem_y = ty - (int32_t)sy * 256;

	if (sx > OUT_MAX) {
		sx = OUT_MAX;
	}
	if (sx < -OUT_MAX) {
		sx = -OUT_MAX;
	}
	if (sy > OUT_MAX) {
		sy = OUT_MAX;
	}
	if (sy < -OUT_MAX) {
		sy = -OUT_MAX;
	}

	*out_x = sx;
	*out_y = sy;
}