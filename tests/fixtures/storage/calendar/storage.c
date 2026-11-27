// SPDX-License-Identifier: MIT
#include "host_api.h"
static int32_t refused_file_write(const char *, int32_t, int32_t, const void *, int32_t);
#define file_write refused_file_write
#include "production.h"
#undef file_write

static int32_t refused_file_write(const char *name, int32_t name_length, int32_t offset,
                                  const void *bytes, int32_t length)
{
    (void)name; (void)name_length; (void)offset; (void)bytes; (void)length;
    return HOST_ERR_NO_SPACE;
}
