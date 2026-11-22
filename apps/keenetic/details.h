// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#define KEENETIC_HOSTS_MAX 32
/* Retain the SDK picker's full 63-byte name before screen-width fitting. */
typedef struct { char name[64], ip[40]; } keenetic_host_t;
typedef struct {
    keenetic_host_t rows[KEENETIC_HOSTS_MAX];
    int count, total;
} keenetic_hosts_t;
/* Lists have the observed complete-field router contract. See README for
 * the SDK's missing-field and boolean/string limitations. */
void keenetic_hosts_read(keenetic_hosts_t *hosts);
