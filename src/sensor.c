/*-
 * Copyright (c) 2020 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/console.h>
#include <sys/callout.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/sem.h>

#include <arm/arm/nvic.h>
#include <arm/nordicsemi/nrf9160.h>

#include <dev/mc6470/mc6470.h>

#include "board.h"
#include "sensor.h"

#include <lib/msun/src/math.h>

static mdx_sem_t sem;
static mdx_device_t i2c;
static mdx_device_t gpiote;

void
mc6470_intr(void *arg, int irq)
{

	mdx_sem_post(&sem);
}

static void
mc6470_ecompass(int16_t mag_x, int16_t mag_y, int16_t mag_z,
    int16_t acc_x, int16_t acc_y, int16_t acc_z)
{
	double pitch, roll, azimuth;
	double X_h, Y_h;
	int16_t p, r, az;

	/* Calculate pitch and roll, in the range (-pi, pi). */
	pitch = atan2((double) - acc_x,
	    sqrt((long)acc_z * (long)acc_z + (long)acc_y * (long)acc_y));
	roll = atan2((double)acc_y,
	    sqrt((long)acc_z * (long)acc_z + (long)acc_x * (long)acc_x));

	X_h = (double)mag_x * cos(pitch) +
	    (double)mag_y * sin(roll) * sin(pitch) +
	    (double)mag_z * cos(roll) * sin(pitch);

	Y_h = (double)mag_y * cos(roll) -
	    (double)mag_z * sin(roll);

	azimuth = atan2(Y_h, X_h);
	if(azimuth < 0)	/* Convert Azimuth in the range (0, 2pi) */
		azimuth = 2 * M_PI + azimuth;

	az = (int16_t)(azimuth * 180.0 / M_PI);
	p = (int16_t)(pitch * 180.0 / M_PI);
	r = (int16_t)(roll * 180.0 / M_PI);

	printf("pitch %3d, roll %3d, azimuth %3d\n", p, r, az);
}

static void
mc6470_process(void)
{
	int16_t mag_x, mag_y, mag_z;
	int16_t acc_x, acc_y, acc_z;
	uint8_t vals[6];
	uint8_t ctrl1;
	float xf, yf;
	float a;

	mc6470_read_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL1, &ctrl1);

	bzero(vals, 6);
	mc6470_read_data(i2c, MC6470_MAG, MC6470_MAG_XOUTL, 6, vals);
	mag_x = vals[1] << 8 | vals[0] << 0;
	mag_y = vals[3] << 8 | vals[2] << 0;
	mag_z = vals[5] << 8 | vals[4] << 0;

	bzero(vals, 6);
	mc6470_read_data(i2c, MC6470_ACC, MC6470_XOUT_EX_L, 6, vals);
	acc_x = vals[1] << 8 | vals[0] << 0;
	acc_y = vals[3] << 8 | vals[2] << 0;
	acc_z = vals[5] << 8 | vals[4] << 0;

	if (1 == 0) {
		printf("%d/%d, %d/%d, %d/%d\n",
		    vals[0], vals[1],
		    vals[2], vals[3],
		    vals[4], vals[5]);
		return;
	}

	if (1 == 0) {
		printf("%d,%d,%d\n", mag_x, mag_y, mag_z);
		return;
	}

	if (1 == 0) {
		xf = mag_x * 0.15f;
		yf = mag_y * 0.15f;
		a = atan2(yf, xf) * 180 / M_PI;

		printf("x %d, y %d, z %d, heading %.2f\n",
		    mag_x, mag_y, mag_z, a);
		return;
	}

	mc6470_ecompass(mag_x, mag_y, mag_z, acc_x, acc_y, acc_z);
}

static void
mc6470_thread(void *arg)
{
	uint8_t val;

	while (1) {
		mdx_sem_wait(&sem);

		//printf("%s: event received\n", __func__);

		/* Ack the event by reading SR register. */
		mc6470_read_reg(i2c, MC6470_ACC, MC6470_SR, &val);
	}
}

void
sensor_init(void)
{
	int16_t xoffs, yoffs, zoffs;
	struct thread *td;
	uint8_t val;
	uint8_t reg;

	mdx_sem_init(&sem, 0);
	td = mdx_thread_create("mc6470", 1, 0, 4096, mc6470_thread, NULL);
	mdx_sched_add(td);

	i2c = mdx_device_lookup_by_name("nrf_twim", 0);
	if (i2c == NULL)
		panic("could not find twim device");

	gpiote = mdx_device_lookup_by_name("nrf_gpiote", 0);
	if (gpiote == NULL)
		panic("could not find gpiote device");

	nrf_gpiote_setup_intr(gpiote, MC6470_GPIOTE_CFG_ID,
	    mc6470_intr, NULL);
	nrf_gpiote_intctl(gpiote, MC6470_GPIOTE_CFG_ID, true);

	mc6470_write_reg(i2c, MC6470_ACC, MC6470_MODE, MODE_OPCON_STANDBY);
	mdx_usleep(10000);

	mc6470_write_reg(i2c, MC6470_ACC, MC6470_SRTFR, SRTFR_RATE_64HZ);
	mc6470_read_reg(i2c, MC6470_ACC, MC6470_SRTFR, &val);
	printf("%s: val %x\n", __func__, val);

	reg = INTEN_TIXPEN | INTEN_TIXNEN;
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_INTEN, reg);
	reg = TAPEN_TAPXPEN | TAPEN_TAPXNEN | TAPEN_TAP_EN | TAPEN_THRDUR;
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_TAPEN, reg);
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_TTTRX, 4);
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_TTTRY, 4);
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_TTTRZ, 4);
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_OUTCFG, OUTCFG_RANGE_2G);
	mc6470_write_reg(i2c, MC6470_ACC, MC6470_MODE, MODE_OPCON_WAKE);
	mdx_usleep(10000);

	/* Ack any stale events by reading SR register. */
	mc6470_read_reg(i2c, MC6470_ACC, MC6470_SR, &val);

	/* Magnetometer. */
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL4,
	    0x80 | MAG_CTRL4_RS);

	mc6470_read_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL1, &val);
	val &= ~MAG_CTRL1_FS;
	val |= MAG_CTRL1_PC;
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL1, val);

	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL4,
	    0x80 | MAG_CTRL4_RS);

	xoffs = 228;
	yoffs = -859;
	zoffs = 274;

	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_XOFFL, xoffs & 0xff);
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_XOFFH, xoffs >> 8);
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_YOFFL, yoffs & 0xff);
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_YOFFH, yoffs >> 8);
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_ZOFFL, zoffs & 0xff);
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_ZOFFH, zoffs >> 8);

#if 0
	mc6470_read_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL3, &val);
	val |= MAG_CTRL3_OCL;
	mc6470_write_reg(i2c, MC6470_MAG, MC6470_MAG_CTRL3, val);
	mdx_usleep(5000000);
#endif
}

void
sensor_test(void)
{

	while (1) {
		mc6470_process();
		mdx_usleep(100000);
	}
}
