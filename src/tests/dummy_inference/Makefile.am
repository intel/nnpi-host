#
# Copyright (C) 2017-2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

bin_PROGRAMS = dummy_inference

dummy_inference_SOURCES = \
	dummy_inference.cpp \
	infer_network.cpp

dummy_inference_CXXFLAGS = \
	-I$(top_srcdir)/src/include

dummy_inference_LDADD = $(top_builddir)/src/nnpi_drv/libnnpi_drv.la
