/*-
 * Copyright (c) 2018-2020 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/of.h>

#include <arm/arm/nvic.h>
#include <arm/nordicsemi/nrf9160.h>

#include <lib/libfdt/libfdt.h>

#include <dev/gpio/gpio.h>
#include <dev/intc/intc.h>
#include <dev/uart/uart.h>

#include "board.h"
#include "sensor.h"

void
board_init(void)
{
	struct nrf_twim_conf conf;
	struct nrf_gpiote_conf gconf;

	/* Add some memory so devices could allocate softc. */
	mdx_fl_init();
	mdx_fl_add_region(0x20004000, 0x0c000);
	mdx_fl_add_region(0x20030000, 0x10000);

	nrf_uarte_init(&devs.uart, BASE_UARTE0, UART_PIN_TX, UART_PIN_RX);
	mdx_uart_setup(&devs.uart, UART_BAUDRATE, UART_DATABITS_8,
	    UART_STOPBITS_1, UART_PARITY_NONE);
	mdx_console_register_uart(&devs.uart);

	nrf_power_init(&devs.power, BASE_POWER);
	nrf_timer_init(&devs.timer, BASE_TIMER0, 1000000);
	nrf_gpio_init(&devs.gpio, BASE_GPIO);
	nrf_gpiote_init(&devs.gpiote, BASE_GPIOTE1);

	arm_nvic_init(&devs.nvic, BASE_SCS);

	conf.freq = TWIM_FREQ_K100;
	conf.pin_scl = PIN_MC_SCL;
	conf.pin_sda = PIN_MC_SDA;

	nrf_twim_init(&devs.i2c, BASE_TWIM1);
	nrf_twim_setup(&devs.i2c, &conf);

	nrf_gpio_pincfg(&devs.gpio, PIN_MC_INTA, 0);
	mdx_gpio_configure(&devs.gpio, 0, PIN_MC_INTA, MDX_GPIO_INPUT);

	/* Configure GPIOTE for mc6470. */
	gconf.pol = GPIOTE_POLARITY_HITOLO;
	gconf.mode = GPIOTE_MODE_EVENT;
	gconf.pin = PIN_MC_INTA;
	nrf_gpiote_config(&devs.gpiote, MC6470_GPIOTE_CFG_ID, &gconf);

	mdx_intc_setup(&devs.nvic, ID_UARTE0, nrf_uarte_intr, devs.uart.sc);
	mdx_intc_setup(&devs.nvic, ID_TIMER0, nrf_timer_intr, devs.timer.sc);
	mdx_intc_setup(&devs.nvic, ID_TWIM1, nrf_twim_intr, devs.i2c.sc);
	mdx_intc_setup(&devs.nvic, ID_GPIOTE1, nrf_gpiote_intr, devs.gpiote.sc);

	mdx_intc_enable(&devs.nvic, ID_TIMER0);
	mdx_intc_enable(&devs.nvic, ID_UARTE0);
	mdx_intc_enable(&devs.nvic, ID_TWIM1);
	mdx_intc_enable(&devs.nvic, ID_GPIOTE1);

	printf("mdepx initialized\n");
}
