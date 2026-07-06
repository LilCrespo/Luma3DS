#ifndef SPOTIFY3DS_MBEDTLS_CONFIG_H
#define SPOTIFY3DS_MBEDTLS_CONFIG_H

/*
    Config mínima para TLS 1.2 cliente en Rosalina.

    Objetivo:
    - TLS cliente
    - transporte propio con miniSoc callbacks
    - sin sockets internos de mbedTLS
    - sin CA verification de momento
    - solo ECDHE_RSA para reducir tamaño
*/

#define MBEDTLS_HAVE_ASM

/* Memory allocator propio para evitar usar heap normal de Rosalina */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* Base crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
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

/* RSA necesita al menos una versión PKCS#1 */
#define MBEDTLS_PKCS1_V15

/* Curvas / ECDH / ECDSA */
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NO_INTERNAL_RNG
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* AES-GCM para suites modernas */
#define MBEDTLS_GCM_C

/* TLS cliente */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* X509 parse, aunque no verifiquemos CA todavía */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* Key exchange */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* Error strings para poder ver fallos */
#define MBEDTLS_ERROR_C

/* Reduce RAM */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096

#include "mbedtls/check_config.h"

#endif