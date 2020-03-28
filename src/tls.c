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

#include "tls.h"

static char buf[1024];

#define	HTTPS_SERVER_NAME	"machdep.uk"
#define	HTTPS_PORT		443
#define	HTTPS_PATH		"/static/hello"

#define TLS_DEBUG
#undef	TLS_DEBUG

#define	TLS_UNSAFE

/* Personalization string for the drbg. */
static const char *DRBG_PERS = "mdep TLS helloword client";

static const char SSL_CA_PEM[] =
/* Let's Encrypt Root Certificate */
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

#define	DEBUG_LEVEL	4

static int
make_fd(void)
{
	struct nrf_addrinfo *server_addr;
	int err;
	int fd;

	fd = nrf_socket(NRF_AF_INET, NRF_SOCK_STREAM, 0);
	if (fd < 0) {
		printf("failed to create socket\n");
		return (-1);
	}

	nrf_freeaddrinfo(server_addr);
	err = nrf_getaddrinfo(HTTPS_SERVER_NAME, NULL, NULL, &server_addr);
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

	s->sin_port = nrf_htons(HTTPS_PORT);
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

	printf("Successfully connected to the TLS server.\n");

	//nrf_fcntl(fd, NRF_F_SETFL, NRF_O_NONBLOCK);

	return (fd);
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

#ifdef TLS_DEBUG
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
		printf("%s\n", buf);
	}
 
	return 0;
}
#endif

int
tls_test(void)
{
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_x509_crt cacert;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config ssl_conf;
	int bpos;
	int err;
	int fd;

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&ssl_conf);

	bpos = snprintf(buf, sizeof(buf) - 1,
	    "GET %s HTTP/1.1\nHost: %s\n\n", HTTPS_PATH, HTTPS_SERVER_NAME);

	printf("bpos %d, buf %s\n", bpos, buf);

	err = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
	    (const unsigned char *)DRBG_PERS, sizeof (DRBG_PERS));
	if (err) {
		printf("failed to make drbg\n");
		return (-1);
	}

	err = mbedtls_x509_crt_parse(&cacert,
	    (const unsigned char *)SSL_CA_PEM, sizeof(SSL_CA_PEM));
	if (err) {
		printf("failed to parse cacert, err %d\n", err);
		return (-1);
	}

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

#ifdef TLS_UNSAFE
	mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
#endif

#ifdef TLS_DEBUG
	/* Debug */
	mbedtls_ssl_conf_verify(&ssl_conf, my_verify, NULL);
	mbedtls_ssl_conf_dbg(&ssl_conf, my_debug, NULL);
	mbedtls_debug_set_threshold(DEBUG_LEVEL);
#endif

	err = mbedtls_ssl_setup(&ssl, &ssl_conf);
	if (err) {
		printf("failed setup ssl, err %d\n", err);
		return (-1);
	}

	fd = make_fd();
	mbedtls_ssl_set_hostname(&ssl, HTTPS_SERVER_NAME);
	mbedtls_ssl_set_bio(&ssl, (void *)fd, ssl_send, ssl_recv, NULL);

	err = mbedtls_ssl_handshake(&ssl);
	if (err) {
		printf("failed to start handshake, err %d\n", err);
		return (-1);
	}

	err = mbedtls_ssl_write(&ssl, (const unsigned char *)buf, bpos);
        if (err < 0) {
		if (err != MBEDTLS_ERR_SSL_WANT_READ &&
		    err != MBEDTLS_ERR_SSL_WANT_WRITE) {
			printf("%s: ssl write error %d\n", __func__, err);
			return (-1);
		}
	}
 
	printf("TLS connection to %s established\r\n", HTTPS_SERVER_NAME);
 
	char cbuf[1024];
	mbedtls_x509_crt_info(cbuf, sizeof(cbuf), "\r    ",
	    mbedtls_ssl_get_peer_cert(&ssl));
	mbedtls_printf("Server certificate:\r\n%s\r", cbuf);

	printf("Certificate verified");
 
	/* Read data out of the socket */
	err = mbedtls_ssl_read(&ssl, (unsigned char *)buf, sizeof(buf));
	if (err < 0) {
		if (err != MBEDTLS_ERR_SSL_WANT_READ &&
		    err != MBEDTLS_ERR_SSL_WANT_WRITE) {
			printf("%s: ssl read error %d\n", __func__, err);
			return (-1);
		}
	}

	printf("Received data: %s\n", buf);

	printf("%s: ok\n", __func__);

	while (1);

	return (0);
}
