#ifndef SPOTIFY_MBEDTLS_CONFIG_H
#define SPOTIFY_MBEDTLS_CONFIG_H

/*
    Minimal mbedTLS configuration for TLS 1.2 client support in Rosalina.

    Goals:
    - TLS client mode only.
    - External transport through miniSoc callbacks.
    - No internal mbedTLS socket layer.
    - No CA certificate verification.
    - Small binary size suitable for Rosalina.
*/

#define MBEDTLS_HAVE_ASM

/* Use a private static allocator instead of Rosalina's normal heap. */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* Core cryptography. */
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C

/* RSA support requires at least one PKCS#1 mode. */
#define MBEDTLS_PKCS1_V15

/* Elliptic-curve support for modern Spotify TLS handshakes. */
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NO_INTERNAL_RNG
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* AES-GCM cipher suites. */
#define MBEDTLS_GCM_C

/* TLS client support. */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* X.509 parsing is required for the TLS handshake, even without CA verification. */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* Key exchange methods required by Spotify endpoints. */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* Keep error strings enabled for readable failure diagnostics. */
#define MBEDTLS_ERROR_C

/* Reduce memory usage. */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096

#include "mbedtls/check_config.h"

#endif