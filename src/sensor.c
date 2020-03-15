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

#include <arm/arm/nvic.h>
#include <arm/nordicsemi/nrf9160.h>

#include <dev/mc6470/mc6470.h>

#include "board.h"
#include "sensor.h"

extern struct nrf_twim_softc twim1_sc;
static struct mc6470_dev dev;
static struct i2c_bus i2cb;

void
sensor_init(void)
{
	uint8_t reg;
	uint8_t val;

	i2cb.xfer = nrf_twim_xfer;
	i2cb.arg = &twim1_sc;

	dev.i2cb = &i2cb;

	mc6470_write_reg(&dev, MC6470_MODE, MODE_OPCON_STANDBY);
	mdx_usleep(10000);

	mc6470_write_reg(&dev, MC6470_SRTFR, SRTFR_RATE_64HZ);
	mc6470_read_reg(&dev, MC6470_SRTFR, &val);
	printf("%s: val %x\n", __func__, val);

	reg = TAPEN_TAPXPEN | TAPEN_TAPXNEN | TAPEN_TAP_EN | TAPEN_THRDUR;
	mc6470_write_reg(&dev, MC6470_TAPEN, reg);
	mc6470_write_reg(&dev, MC6470_TTTRX, 4);
	mc6470_write_reg(&dev, MC6470_TTTRY, 4);
	mc6470_write_reg(&dev, MC6470_TTTRZ, 4);
	mc6470_write_reg(&dev, MC6470_OUTCFG, OUTCFG_RANGE_2G);
	mc6470_write_reg(&dev, MC6470_MODE, MODE_OPCON_WAKE);
	mdx_usleep(10000);

	while (1) {
		mc6470_read_reg(&dev, MC6470_SR, &val);
		if (val != 0 && val != 0x80)
			printf("%s: sr %x\n", __func__, val);
	}
}
