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

#define	DEBUG_LEVEL	4
#define MBEDTLS_DEBUG
#undef	MBEDTLS_DEBUG

/* Personalization string for the drbg. */
static const char *DRBG_PERS = "mdep TLS helloword client";

static const char SSL_CA_PEM[] =
/* Let’s Encrypt Authority X3 (IdenTrust cross-signed) */
"-----BEGIN CERTIFICATE-----\n"
"MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n"
"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n"
"DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n"
"SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n"
"GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n"
"AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n"
"q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n"
"SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n"
"Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n"
"a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n"
"/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n"
"AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n"
"CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n"
"bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n"
"c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n"
"VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n"
"ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n"
"MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n"
"Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n"
"AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n"
"uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n"
"wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n"
"X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n"
"PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n"
"KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n"
"-----END CERTIFICATE-----\n";

static int
make_fd(void)
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

#ifdef MBEDTLS_UNSAFE
	mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
#endif

#ifdef MBEDTLS_DEBUG
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
 
	printf("TLS connection to %s established\n", HTTPS_SERVER_NAME);
 
	char cbuf[1024];
	mbedtls_x509_crt_info(cbuf, sizeof(cbuf), "    ",
	    mbedtls_ssl_get_peer_cert(&ssl));
	mbedtls_printf("Server certificate:\n%s\n", cbuf);

	printf("Certificate verified\n");
 
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
