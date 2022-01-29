#!/usr/bin/env bash
rm -rf build
rm -rf install
mkdir build
mkdir install
cd build
build_type=$1
if [ -z "${build_type}" ]; then
  echo "default build type to RelWithDebInfo"
  build_type="relwithdeb"
fi

echo "building and installing libromp with "$build_type

if [ $build_type == "debug" ]; then
  cmake -DCMAKE_CXX_FLAGS="-std=c++17 -g -DDEBUG" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
fi

if [ $build_type == "relwithdeb" ]; then
  cmake -DCMAKE_CXX_FLAGS="-std=c++17 -g -O2" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
fi 

if [ $build_type == "release" ]; then
  cmake -DCMAKE_CXX_FLAGS="-std=c++17 -O3" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
fi 

make 
make install
cd ..

