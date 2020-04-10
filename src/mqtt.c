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

#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>

#include <littlefs/lfs.h>

#include <cJSON/cJSON.h>
#include <mqtt/mqtt.h>
#include "mqtt.h"
#include "board.h"

#define	TCP_HOST	"akc28iu7dn5ra-ats.iot.eu-west-2.amazonaws.com"
#define	TCP_PORT	8883

#define	IOT_SSL_READ_TIMEOUT	10
#define	DEBUG_LEVEL		4
#define MBEDTLS_DEBUG
#undef	MBEDTLS_DEBUG

static struct mqtt_client client;
static mbedtls_ssl_context ssl;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_x509_crt cacert, clicert;
static mbedtls_pk_context pkey;
static mbedtls_ssl_config ssl_conf;

static void mqtt_event(struct mqtt_client *c,
    enum mqtt_connection_event ev);
static void mqtt_cb(struct mqtt_client *c, struct mqtt_request *m);
static mdx_sem_t sem_reconn;

/* Personalization string for the drbg. */
static const char *DRBG_PERS = "mdep secure mqtt client";

static lfs_t lfs;

static int
disk_read(const struct lfs_config *c, lfs_block_t block,
    lfs_off_t off, void *buffer, lfs_size_t size)
{
	void *addr;

	addr = (void *)(DISK_ADDRESS + block * c->block_size + off);

#if 0
	printf("%s: block %d off %x size %d, address %p\n",
	    __func__, block, off, size, addr);
	printf("word %x\n", *(uint32_t *)addr);
#endif

	memcpy(buffer, addr, size);

	return (0);
}

const struct lfs_config cfg = {
	/* block device operations */
	.read  = disk_read,
#if 0
	.prog  = disk_prog,
	.erase = disk_erase,
	.sync  = disk_sync,
#endif

	/* block device configuration */
	.read_size = 16,
	.prog_size = 16,
	.block_size = 512,
	.block_count = 32,
	.cache_size = 16,
	.lookahead_size = 16,
	.block_cycles = 500,
};

static int
read_file(const char *filename, void **addr, uint32_t *size)
{
	static lfs_file_t file;
	uint8_t *ptr;
	int err;

	err = lfs_mount(&lfs, &cfg);
	if (err) {
		printf("%s: could not mount\n", __func__);
		return (err);
	}

	err = lfs_file_open(&lfs, &file, filename, LFS_O_RDONLY);
	if (err) {
		printf("%s: could not open file, err %d\n", __func__, err);
		return (err);
	}

	ptr = malloc(file.ctz.size + 1);
	if (ptr == NULL) {
		printf("%s: could not allocate %d bytes\n",
		    __func__, file.ctz.size + 1);
		return (-1);
	}

	err = lfs_file_read(&lfs, &file, ptr, file.ctz.size);
	if (err != file.ctz.size) {
		printf("%s: could not read file, err %d\n", __func__, err);
		return (err);
	}

	err = lfs_unmount(&lfs);
	if (err)
		printf("%s: could not unmount\n", __func__);

	/* Make mbedtls happy. */
	ptr[file.ctz.size] = '\0';

	*size = file.ctz.size + 1;
	*addr = ptr;

	return (0);
}

static int
mqtt_tcp_connect(int fd)
{
	struct nrf_addrinfo *server_addr;
	int err;

	printf("%s: freeing the address\n", __func__);

	nrf_freeaddrinfo(server_addr);

	printf("%s: nrf_getaddrinfo\n", __func__);
	err = nrf_getaddrinfo(TCP_HOST, NULL, NULL, &server_addr);
	if (err != 0) {
		printf("getaddrinfo failed with error %d\n", err);
		return (-1);
	}
	printf("%s: nrf_getaddrinfo done\n", __func__);

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
		return (-1);
	}

	printf("Connecting to server...\n");
	err = nrf_connect(fd, s,
	    sizeof(struct nrf_sockaddr_in));
	if (err != 0) {
		printf("TCP connect failed: err %d\n", err);
		return (-1);
	}

	printf("Successfully connected to the MQTT server, fd %d\n", fd);

	return (0);
}

static int
net_read(struct mqtt_network *net, uint8_t *buf, int len)
{
	size_t err;

	printf("%s: len %d\n", __func__, len);
	err = mbedtls_ssl_read(&ssl, (unsigned char *)buf, len); //sizeof(buf));
	printf("%s: err %d\n", __func__, err);

	if (err == MBEDTLS_ERR_SSL_TIMEOUT)
		err = -1;

	return (err);
}

static int
net_write(struct mqtt_network *net, uint8_t *buf, int len)
{
	size_t err;

	printf("%s: len %d\n", __func__, len);
	err = mbedtls_ssl_write(&ssl, (const unsigned char *)buf, len);

#if 0
	if (err != MBEDTLS_ERR_SSL_WANT_READ &&
	    err != MBEDTLS_ERR_SSL_WANT_WRITE) {
#endif

	printf("%s: err %d\n", __func__, err);

	return (err);
}

static int
ssl_recv(void *arg, unsigned char *buf, size_t len)
{
	int err;
	int fd;

	fd = (int)arg;

	printf("%s: len %d\n", __func__, len);
	err = nrf_read(fd, buf, len);
	printf("%s: err %d\n", __func__, err);

	return (err);
}

static void
cb(void *arg)
{
	int *complete;

	complete = arg;

	//printf("%s\n", __func__);

	*complete = 1;
}

static int
ssl_recv_timeout(void *arg, unsigned char *buf, size_t len, uint32_t timeout)
{
	mdx_callout_t c;
	int err;
	int fd;
	int complete;

	fd = (int)arg;

	complete = 0;
	mdx_callout_init(&c);
	mdx_callout_set(&c, timeout * 1000000, cb, &complete);

	printf("%s: len %d, timeout %d\n", __func__, len, timeout);

	do {
		err = nrf_read(fd, buf, len);
		if (err > 0) {
			/* Data received */
			break;
		}

		if (err == 0) {
			/* Connection closed */
			break;
		}
	} while (complete == 0);

	printf("%s: err %d, complete %d\n", __func__, err, complete);

	critical_enter();
	mdx_callout_cancel(&c);
	critical_exit();

	if (err == 0) {
		/* Connection closed */
	} else if (complete == 1) {
		/* Timeout */
		err = MBEDTLS_ERR_SSL_TIMEOUT;
	}

	return (err);
}

static int
ssl_send(void *arg, const unsigned char *buf, size_t len)
{
	int err;
	int fd;

	fd = (int)arg;

	printf("%s: len %d\n", __func__, len);
	err = nrf_write(fd, buf, len);
	printf("%s: err %d\n", __func__, err);

	return (err);
}

#ifdef MBEDTLS_DEBUG
static void
my_debug(void *arg, int level, const char *file,
    int line, const char *str)
{
	const char *p, *basename;

	/* Extract basename from filename. */
	for (p = basename = file; *p != '\0'; p++)
		if (*p == '/' || *p == '\\')
			basename = p + 1;

	printf("%s:%04d: |%d| %s", basename, line, level, str);
}

static int
my_verify(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
	char buf[1024];

	printf("\nVerifying certificate at depth %d:\n", depth);
	mbedtls_x509_crt_info(buf, sizeof (buf) - 1, "  ", crt);
	printf("%s", buf);

	if (*flags == 0)
		printf("No verification issue for the certificate.\n");
	else {
		mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", *flags);
		printf("%s: %s\n", __func__, buf);
	}

	return 0;
}
#endif

static int
mqtt_handshake(int fd)
{
	char cbuf[1024];
	uint32_t size;
	void *addr;
	int err;

	memset(&ssl_conf, 0, sizeof(mbedtls_ssl_config));

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_x509_crt_init(&clicert);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&ssl_conf);
	mbedtls_pk_init(&pkey);

	err = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
	    (const unsigned char *)DRBG_PERS, sizeof (DRBG_PERS));
	if (err) {
		printf("failed to make drbg\n");
		return (-1);
	}

	err = read_file("rootca.pem", &addr, &size);
	if (err) {
		printf("could not read rootca, err %d\n", err);
		return (-1);
	}
	err = mbedtls_x509_crt_parse(&cacert, addr, size);
	free(addr);
	if (err) {
		printf("failed to parse cacert, err %d\n", err);
		return (-1);
	}
	printf("rootca size %d\n", size);

	err = mbedtls_ssl_config_defaults(&ssl_conf,
	    MBEDTLS_SSL_IS_CLIENT,
	    MBEDTLS_SSL_TRANSPORT_STREAM,
	    MBEDTLS_SSL_PRESET_DEFAULT);
	if (err) {
		printf("failed to config ssl defaults, err %d\n", err);
		return (-1);
	}

	mbedtls_ssl_conf_ca_chain(&ssl_conf, &cacert, NULL);
	mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);

	err = read_file("private_key.pem", &addr, &size);
	if (err) {
		printf("could not read private key, err %d\n", err);
		return (-1);
	}
	err = mbedtls_pk_parse_key(&pkey, addr, size, NULL, 0);
	free(addr);
	if (err) {
		printf("could not parse pk key, err %d\n", err);
		return (-1);
	}
	printf("pkey size %d\n", size);

	err = read_file("certificate.pem", &addr, &size);
	if (err) {
		printf("could not read certificate, err %d\n", err);
		return (-1);
	}
	err = mbedtls_x509_crt_parse(&clicert, addr, size);
	free(addr);
	if (err) {
		printf("could not read certificate, err %d\n", err);
		return (-1);
	}
	printf("clicert size %d\n", size);

	err = mbedtls_ssl_conf_own_cert(&ssl_conf, &clicert, &pkey);
	if (err) {
		printf("failed to set own cert, err %d\n", err);
		return (-1);
	}

#ifdef MBEDTLS_UNSAFE
	mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
#else
	mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
#endif

#ifdef MBEDTLS_DEBUG
	/* Debug */
	mbedtls_ssl_conf_verify(&ssl_conf, my_verify, NULL);
	mbedtls_ssl_conf_dbg(&ssl_conf, my_debug, NULL);
	mbedtls_debug_set_threshold(DEBUG_LEVEL);
#endif

	mbedtls_ssl_conf_read_timeout(&ssl_conf, 10);
	mbedtls_ssl_conf_handshake_timeout(&ssl_conf, 5, 15);

	err = mbedtls_ssl_setup(&ssl, &ssl_conf);
	if (err) {
		printf("failed to setup ssl, err %d\n", err);
		return (-1);
	}

	mbedtls_ssl_set_hostname(&ssl, TCP_HOST);
	mbedtls_ssl_set_bio(&ssl, (void *)fd,
	    ssl_send, ssl_recv, ssl_recv_timeout);

	err = mbedtls_ssl_handshake(&ssl);
	if (err) {
		printf("Failed to handshake, err %d\n", err);
		return (-1);
	}

	err = mbedtls_ssl_get_record_expansion(&ssl);
	if (err >= 0)
		printf("Record expansion is %d\n", err);
	else
		printf("Record expansion is unknown (compression)\n");

	printf("MQTT handshake with %s succeeded\n", TCP_HOST);

	printf("Ciphersuite is %s\n", mbedtls_ssl_get_ciphersuite(&ssl));

	int flags;

	printf("Verifying peer X.509 certificate...\n");
	flags = mbedtls_ssl_get_verify_result(&ssl);
	printf("Flags is %x\n", flags);

	mbedtls_x509_crt_info(cbuf, sizeof(cbuf), "    ",
	    mbedtls_ssl_get_peer_cert(&ssl));
	mbedtls_printf("Server certificate:\n%s\n", cbuf);

	return (0);
}

static int
mqtt_ssl_connect(struct mqtt_network *net)
{
	int err;

	printf("%s: trying to connect\n", __func__);

	net->fd = nrf_socket(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP);
	if (net->fd < 0) {
		printf("failed to create socket\n");
		return (-1);
	}

	err = mqtt_tcp_connect(net->fd);
	if (err != 0) {
		nrf_close(net->fd);
		printf("Failed to connect to the TCP server, err %d\n", err);
		return (-1);
	}

	nrf_fcntl(net->fd, NRF_F_SETFL, NRF_O_NONBLOCK);

	err = mqtt_handshake(net->fd);
	if (err != 0) {
		nrf_close(net->fd);
		printf("Failed to handshake, err %d\n", err);
		return (-1);
	}

	return (0);
}

static int
client_initialize(struct mqtt_client *c)
{
	struct mqtt_network *net;
	int err;

	memset(&client, 0, sizeof(struct mqtt_client));
	client.event = mqtt_event;
	client.msgcb = mqtt_cb;
	net = &client.net;
	net->fd = -1;
	net->read = net_read;
	net->write = net_write;
	err = mqtt_init(&client);
	if (err) {
		printf("Could not initialize MQTT client, err %d\n", err);
		return (-1);
	};

	return (0);
}

static int
mqtt_test_subscribe(void)
{
	struct mqtt_request s;
	int err;

	memset(&s, 0, sizeof(struct mqtt_request));
	s.topic = "test/test";
	s.topic_len = 9;
	s.qos = 0;
	err = mqtt_subscribe(&client, &s);
	if (err != 0) {
		printf("%s: cant subscribe\n", __func__);
		return (-1);
	}

	printf("%s: subscribe succeeded\n", __func__);

	return (0);
}

static int
mqtt_test_publish(void)
{
	struct mqtt_request m;
	int err;

	memset(&m, 0, sizeof(struct mqtt_request));
	m.qos = 1;
	m.data = "test message";
	m.data_len = 12;
	m.topic = "test/test";
	m.topic_len = 9;

	err = mqtt_publish(&client, &m);
	if (err != 0) {
		printf("%s: can't publish\n", __func__);
		return (-1);
	}

	printf("%s: publish succeeded\n", __func__);

	return (0);
}

static void
mqtt_thread(void *arg)
{
	struct mqtt_network *net;
	struct mqtt_client *c;
	int err;
	int retry;

	c = arg;
	net = &c->net;

	retry = 0;

	while (1) {
		printf("%s: Waiting for a semaphore...\n", __func__);
		mdx_sem_wait(&sem_reconn);

		printf("%s: Freeing SSL configuration\n", __func__);

		mbedtls_entropy_free(&entropy);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_x509_crt_free(&cacert);
		mbedtls_x509_crt_free(&clicert);
		mbedtls_ssl_free(&ssl);
		mbedtls_ssl_config_free(&ssl_conf);
		mbedtls_pk_free(&pkey);

		printf("%s: trying to SSL connect\n", __func__);
		err = mqtt_ssl_connect(net);
		if (err) {
			printf("%s: Failed to establish SSL conn, err %d\n",
			    __func__, err);

			/* Give up */
			if (retry++ > 10) {
				printf("can't connect, retry count exceeded\n");
				continue;
			}

			mdx_sem_post(&sem_reconn);
			mdx_usleep(1000000);
			continue;
		}

		retry = 0;

		err = mqtt_connect(&client);
		if (err) {
			printf("%s: can't connect to the MQTT broker\n",
			    __func__);
			nrf_close(net->fd);
			mdx_sem_post(&sem_reconn);
			mdx_usleep(1000000);
			continue;
		}

		err = mqtt_test_subscribe();
		if (err) {
			printf("%s: can't subscribe\n",
			    __func__);
			nrf_close(net->fd);
			mdx_sem_post(&sem_reconn);
			mdx_usleep(1000000);
			continue;
		}

		do {
			err = mqtt_test_publish();
			if (err)
				break;
			err = mqtt_poll(c);
			if (err)
				break;
			mdx_usleep(5000000);
			err = mqtt_poll(c);
			if (err)
				break;
		} while (err == 0);

		nrf_close(net->fd);
		mdx_sem_post(&sem_reconn);
		mdx_usleep(1000000);
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

int
mqtt_test(void)
{
	int err;

	err = client_initialize(&client);
	if (err) {
		printf("%s: can't initialize MQTT client\n", __func__);
		return (-3);
	}

	mdx_sem_init(&sem_reconn, 1);

#if 1
	struct thread *td;
	td = mdx_thread_create("mqtt recv", 1, 0, 16384,
	    mqtt_thread, &client);
	if (td == NULL) {
		printf("Failed to create thread\n");
		return (-2);
	}
	mdx_sched_add(td);
	while (1)
		mdx_usleep(1000000);
#else
	mqtt_thread(&client);
#endif

	return (0);
}
