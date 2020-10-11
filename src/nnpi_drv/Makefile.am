#
# Copyright (C) 2017-2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

lib_LTLIBRARIES = libnnpi_drv.la

libnnpi_drv_la_SOURCES = \
	nnpiInference.cpp \
	nnpiCopyCommand.cpp \
	nnpiDevNet.cpp \
	nnpiDevRes.cpp \
	nnpiExecErrorList.cpp \
	nnpiInfContext.cpp \
	nnpiInfReq.cpp \
	nnpiCommandList.cpp \
	nnpiHostProc.cpp \
	nnpiDevice.cpp \
	nnpiChannel.cpp \
	nnpiUtils.cpp

include_HEADERS = \
	../include/nnpdrvInference.h
	../include/nnpdrvTypes.h

libnnpi_drv_la_CXXFLAGS = \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/nnpi_drv/include \
	-I$(top_srcdir)/src/nnpi_drv/include/nnpi_ipc \
	$(PTHREAD_CFLAGS)

libnnpi_drv_la_LIBADD = $(PTHREAD_LIBS)
