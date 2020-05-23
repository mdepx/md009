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

#include <arm/nordicsemi/nrf9160.h>

#include <lib/libfdt/libfdt.h>

#include <dev/gpio/gpio.h>
#include <dev/intc/intc.h>
#include <dev/uart/uart.h>

#include "board.h"
#include "sensor.h"

static struct mdx_device low_uart;

void
board_init(void)
{
	struct nrf_gpiote_conf gconf;
	mdx_device_t nvic, gpio, gpiote, uart;

	/* Add some memory so devices could allocate softc. */
	mdx_fl_init();
	mdx_fl_add_region(0x20004000, 0x0c000);
	mdx_fl_add_region(0x20030000, 0x10000);

	nrf_uarte_init(&low_uart, BASE_UARTE0, UART_PIN_TX, UART_PIN_RX);
	mdx_uart_setup(&low_uart, UART_BAUDRATE, UART_DATABITS_8,
	    UART_STOPBITS_1, UART_PARITY_NONE);
	mdx_console_register_uart(&low_uart);

	mdx_of_install_dtbp((void *)0xf8000);
	mdx_of_probe_devices();

	uart = mdx_device_lookup_by_name("nrf_uarte", 0);
	if (!uart)
		panic("uart dev not found");

	gpio = mdx_device_lookup_by_name("nrf_gpio", 0);
	if (!gpio)
		panic("gpio dev not found");

	nrf_gpio_pincfg(gpio, PIN_MC_INTA, 0);
	mdx_gpio_configure(gpio, 0, PIN_MC_INTA, MDX_GPIO_INPUT);

	/* Configure GPIOTE for mc6470. */
	gpiote = mdx_device_lookup_by_name("nrf_gpiote", 0);
	if (!gpiote)
		panic("gpiote dev not found");

	gconf.pol = GPIOTE_POLARITY_HITOLO;
	gconf.mode = GPIOTE_MODE_EVENT;
	gconf.pin = PIN_MC_INTA;
	nrf_gpiote_config(gpiote, MC6470_GPIOTE_CFG_ID, &gconf);

	nvic = mdx_device_lookup_by_name("nvic", 0);
	if (!nvic)
		panic("nvic dev not found");

	mdx_intc_enable(nvic, ID_TIMER0);
	mdx_intc_enable(nvic, ID_TWIM1);
	mdx_intc_enable(nvic, ID_GPIOTE1);

	printf("mdepx initialized\n");
}
