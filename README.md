# Intel(C) Neural Network Process for Inference (NNP-I) Software

# About
This repository contains the host low-level user-mode library and tests for Intel NNP-I.

## Runtime dependencies

1. libc++

## Building

1. autoreconf -i
2. ./configure
3. make
4. sudo make install

The user-mode library will be installed in /usr/local/lib/libnnpi_drv.so.0.
The dummy_inference test program will be installed in /usr/local/bin/dummy_inference.

## Tests
**dummy_inference** - A test program that runs the entire flow of a real inference application, including
allocating resources, schedule inference commands, DMA input/outputs to/from the device but without running
any real inference. It uses special "ULT" feature of the device which allows to create dummy inference network
that will only copy the network inputs to outputs when the network is executed.
<br/>
The test can schedule one or more such infer commands with configurable number and size of inputs and outputs
and validate the resulting outputs.
<br/>
Run `dummy_inference -h` for usage.
