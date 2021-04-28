#!/usr/bin/env bash
rm -rf build
rm -rf install
mkdir build
mkdir install
cd build
cmake -DCMAKE_CXX_FLAGS=-std=c++17 -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
make 
make install
cd ..

