#!/usr/bin/bash
# This script is only used for cmake configuration stage. One needs to load all # required modules before running this script.
mkdir install
mkdir build && cd build
cmake -DCMAKE_CXX_FLAGS=-std=c++17
      -DCMAKE_CXX_COMPILER=g++
      -DCMAKE_C_COMPILER=gcc
      -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
make
make install
