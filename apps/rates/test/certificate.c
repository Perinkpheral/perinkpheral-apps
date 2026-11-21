// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "standin.h"

int main(void)
{
    standin_reset();
    int32_t length;
    const uint8_t *root = standin_file("russian-root-ca.pem", &length);
    assert(root && length > 0 && length <= 4096);
    assert(length >= 27 && !memcmp(root, "-----BEGIN CERTIFICATE-----", 27));
    FILE *file = fopen("../manifest.json", "rb");
    assert(file);
    char manifest[4096] = {0};
    size_t bytes = fread(manifest, 1, sizeof manifest - 1, file);
    assert(bytes > 0 && !ferror(file) && feof(file));
    fclose(file);
    assert(strstr(manifest, "\"certificates\": [\n    \"russian-root-ca.pem\"\n  ]"));
    puts("Rates: the history host's public CA root is packaged and declared");
    return 0;
}
