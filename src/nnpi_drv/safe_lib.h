/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include <string.h>

#define strcpy_s(dest, size, src)        strncpy((dest), (src), (size))
#define memcpy_s(dest, dmax, src, smax)  memmove((dest), (src), (smax))
