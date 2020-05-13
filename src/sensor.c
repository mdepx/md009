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

extern struct mdx_device dev_gpiote;
extern struct mdx_device dev_i2c;

static mdx_sem_t sem;

void
mc6470_intr(void *arg, int irq)
{

	mdx_sem_post(&sem);
}

static void
mc6470_thread(void *arg)
{
	uint8_t val;

	while (1) {
		mdx_sem_wait(&sem);

		printf("%s: event received\n", __func__);

		/* Ack the event by reading SR register. */
		mc6470_read_reg(&dev_i2c, MC6470_SR, &val);
	}
}

void
sensor_init(void)
{
	struct thread *td;
	uint8_t val;
	uint8_t reg;

	mdx_sem_init(&sem, 0);
	td = mdx_thread_create("mc6470", 1, 0, 4096, mc6470_thread, NULL);
	mdx_sched_add(td);

	nrf_gpiote_setup_intr(&dev_gpiote, MC6470_GPIOTE_CFG_ID,
	    mc6470_intr, NULL);
	nrf_gpiote_intctl(&dev_gpiote, MC6470_GPIOTE_CFG_ID, true);

	mc6470_write_reg(&dev_i2c, MC6470_MODE, MODE_OPCON_STANDBY);
	mdx_usleep(10000);

	mc6470_write_reg(&dev_i2c, MC6470_SRTFR, SRTFR_RATE_64HZ);
	mc6470_read_reg(&dev_i2c, MC6470_SRTFR, &val);
	printf("%s: val %x\n", __func__, val);

	mc6470_write_reg(&dev_i2c, MC6470_INTEN, INTEN_TIXPEN | INTEN_TIXNEN);
	reg = TAPEN_TAPXPEN | TAPEN_TAPXNEN | TAPEN_TAP_EN | TAPEN_THRDUR;
	mc6470_write_reg(&dev_i2c, MC6470_TAPEN, reg);
	mc6470_write_reg(&dev_i2c, MC6470_TTTRX, 4);
	mc6470_write_reg(&dev_i2c, MC6470_TTTRY, 4);
	mc6470_write_reg(&dev_i2c, MC6470_TTTRZ, 4);
	mc6470_write_reg(&dev_i2c, MC6470_OUTCFG, OUTCFG_RANGE_2G);
	mc6470_write_reg(&dev_i2c, MC6470_MODE, MODE_OPCON_WAKE);
	mdx_usleep(10000);

	/* Ack any stale events by reading SR register. */
	mc6470_read_reg(&dev_i2c, MC6470_SR, &val);
}
