#
# Copyright (C) 2017-2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

AC_INIT([nnpi_host], [0.0.1])
AC_CONFIG_AUX_DIR([aux_scripts])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PROG_CXX
AC_PROG_INSTALL
AM_PROG_AR
ACX_PTHREAD

LT_INIT([shared])

AX_APPEND_COMPILE_FLAGS([-std=c++11], [CXXFLAGS])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/nnpi_drv/Makefile
 src/tests/Makefile
 src/tests/dummy_inference/Makefile
])
AC_OUTPUT
