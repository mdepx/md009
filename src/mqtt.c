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
#include <sys/systm.h>

#include <nrfxlib/bsdlib/include/nrf_socket.h>
#include <nrfxlib/bsdlib/include/bsd.h>
#include <nrfxlib/bsdlib/include/bsd_os.h>
#include <nrfxlib/bsdlib/include/bsd_limits.h>

#include <cJSON/cJSON.h>
#include <mqtt/mqtt.h>
#include "mqtt.h"

static struct mqtt_client client;
static int client_connect(struct mqtt_client *c);

#define	TCP_HOST	"test.mosquitto.org"
#define	TCP_PORT	1883

static void mqtt_event(struct mqtt_client *c,
    enum mqtt_connection_event ev);
static void mqtt_cb(struct mqtt_client *c, struct mqtt_request *m);
static mdx_sem_t sem_reconn;

static int
mqtt_fd(void)
{
	struct nrf_addrinfo *server_addr;
	int err;
	int fd;

	fd = nrf_socket(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP);
	if (fd < 0) {
		printf("failed to create socket\n");
		return (-1);
	}

	nrf_freeaddrinfo(server_addr);
	err = nrf_getaddrinfo(TCP_HOST, NULL, NULL, &server_addr);
	if (err != 0) {
		printf("getaddrinfo failed with error %d\n", err);
		nrf_close(fd);
		return (-1);
	}

	struct nrf_sockaddr_in local_addr;
	struct nrf_sockaddr_in *s;
	uint8_t *ip;

	s = (struct nrf_sockaddr_in *)server_addr->ai_addr;
	ip = (uint8_t *)&(s->sin_addr.s_addr);
	printf("Server IP address: %d.%d.%d.%d\n",
	    ip[0], ip[1], ip[2], ip[3]);

	s->sin_port = nrf_htons(TCP_PORT);
	s->sin_len = sizeof(struct nrf_sockaddr_in);

	bzero(&local_addr, sizeof(struct nrf_sockaddr_in));
	local_addr.sin_family = NRF_AF_INET;
	local_addr.sin_port = nrf_htons(0);
	local_addr.sin_addr.s_addr = 0;
	local_addr.sin_len = sizeof(struct nrf_sockaddr_in);

	err = nrf_bind(fd, (struct nrf_sockaddr *)&local_addr,
	    sizeof(local_addr));
	if (err != 0) {
		printf("Bind failed: %d\n", err);
		nrf_close(fd);
		return (-1);
	}

	printf("Connecting to server...\n");
	err = nrf_connect(fd, s,
	    sizeof(struct nrf_sockaddr_in));
	if (err != 0) {
		printf("TCP connect failed: err %d\n", err);
		nrf_close(fd);
		return (-1);
	}

	printf("Successfully connected to the MQTT server.\n");

	nrf_fcntl(fd, NRF_F_SETFL, NRF_O_NONBLOCK);

	return (fd);
}

static int
net_read(struct mqtt_network *net, uint8_t *buffer, int len)
{
	size_t err;

	err = nrf_read(net->fd, buffer, len);

	return (err);
}

static int
net_write(struct mqtt_network *net, uint8_t *buffer, int len)
{
	size_t err;

	err = nrf_write(net->fd, buffer, len);

	return (err);
}

static int
client_connect(struct mqtt_client *c)
{
	struct mqtt_network *net;
	int err;
	int fd;

	fd = mqtt_fd();
	if (fd < 0) {
		printf("Failed to connect to the MQTT server\n");
		return (-1);
	}

	memset(&client, 0, sizeof(struct mqtt_client));
	client.event = mqtt_event;
	client.msgcb = mqtt_cb;
	net = &client.net;
	net->fd = fd;
	net->read = net_read;
	net->write = net_write;
	mqtt_init(&client);

	err = mqtt_connect(&client);
	if (err != 0) {
		printf("%s: can't send MQTT connect message\n", __func__);
		return (-2);
	}

	return (0);
}

static void
mqtt_thread(void *arg)
{
	struct mqtt_network *net;
	struct mqtt_client *c;
	int err;

	c = arg;
	net = &c->net;

	while (1) {
		mdx_sem_wait(&sem_reconn);

		nrf_close(net->fd);
		net->fd = mqtt_fd();
		if (net->fd < 0) {
			printf("%s: failed to create socket\n", __func__);
			mdx_sem_post(&sem_reconn);
			mdx_usleep(1000000);
			continue;
		}

		err = mqtt_connect(&client);
		if (err) {
			printf("%s: can't connect to the MQTT broker\n",
			    __func__);
			mdx_sem_post(&sem_reconn);
			mdx_usleep(1000000);
		}
	}
}

static void
mqtt_event(struct mqtt_client *c, enum mqtt_connection_event ev)
{

	switch (ev) {
	case MQTT_EVENT_CONNECTED:
		printf("%s: connected\n", __func__);
		break;
	case MQTT_EVENT_DISCONNECTED:
		printf("%s: disconnected\n", __func__);
		mdx_sem_post(&sem_reconn);
		break;
	default:
		printf("%s: unknown connection event %d\n", __func__, ev);
	};
}

static void
mqtt_cb(struct mqtt_client *c, struct mqtt_request *m)
{

	printf("%s: message received:\n", __func__);
	printf(" topic: %.*s\n", m->topic_len, m->topic);
	printf(" data: %s\n", m->data);
}

static int
mqtt_test_publish(void)
{
	struct mqtt_request m;
	int err;

	memset(&m, 0, sizeof(struct mqtt_request));
	m.qos = 2;
	m.data = "test";
	m.data_len = 5;
	m.topic = "a/b";
	m.topic_len = 3;

	err = mqtt_publish(&client, &m);
	if (err != 0) {
		printf("%s: can't publish\n", __func__);
		return (-1);
	}

	printf("%s: publish succeeded\n", __func__);

	return (0);
}

static int
mqtt_test_subscribe(void)
{
	struct mqtt_request s;
	int err;

	memset(&s, 0, sizeof(struct mqtt_request));
	s.topic = "a/b";
	s.topic_len = 3;
	s.qos = 1;
	err = mqtt_subscribe(&client, &s);
	if (err != 0) {
		printf("%s: cant subscribe\n", __func__);
		return (-1);
	}

	printf("%s: subscribe succeeded\n", __func__);

	return (0);
}

void
mqtt_test(void)
{
	struct thread *td;
	int err;

	mdx_sem_init(&sem_reconn, 0);

	td = mdx_thread_create("mqtt recv", 1, 0, 8192,
	    mqtt_thread, &client);
	if (td == NULL)
		return;
	mdx_sched_add(td);

	err = client_connect(&client);
	if (err) {
		printf("%s: can't connect to the MQTT broker\n", __func__);
		return;
	}

	err = mqtt_test_subscribe();
	if (err) {
		printf("%s: can't subscribe\n", __func__);
		return;
	}

	while (1) {
		mqtt_test_publish();
		mdx_usleep(70000000);
	}
}
