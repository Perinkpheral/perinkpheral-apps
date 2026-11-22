// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Keenetic 5.01 challenge response. Inputs are terminated, bounded by caller. */
void keenetic_digest(const char *username, const char *realm, const char *password,
                     const char *challenge, char output[65]);
bool keenetic_cookie(const char *header, char output[256]);
bool keenetic_login_body(const char *username, const char digest[65], char output[513]);
