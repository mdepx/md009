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

#include <lib/cJSON/cJSON.h>

#include "sensor.h"
#include "app.h"

static int
ecompass_get(cJSON *obj)
{
	struct ecompass_data data;
	cJSON *p, *r, *az;
	int error;

	error = mc6470_process(&data);
	if (error != 0) {
		printf("Can't get mc6470 data\n");
		return (-1);
	}

	printf("p %3d r %3d az %3d\n",
	    data.pitch, data.roll, data.azimuth);

	p = cJSON_CreateNumber(data.pitch);
	cJSON_AddItemToObject(obj, "pitch", p);

	r = cJSON_CreateNumber(data.roll);
	cJSON_AddItemToObject(obj, "roll", r);

	az = cJSON_CreateNumber(data.azimuth);
	cJSON_AddItemToObject(obj, "azimuth", az);

	return (0);
}

char *
app1(void)
{
	cJSON *obj;
	cJSON *ecompass;
	char *str;

	cJSON_InitHooks(NULL);

	obj = cJSON_CreateObject();
	ecompass = cJSON_CreateObject();

	ecompass_get(ecompass);

	cJSON_AddItemToObject(obj, "ecompass", ecompass);

	str = cJSON_Print(obj);

	printf("Str: %s\n", str);

	cJSON_Delete(obj);

	return (str);
}
