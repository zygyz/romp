#!/bin/bash

#build and install elfutils-0.173
root=`pwd`

cd pkgs-src/elfutils-0.173
mkdir elfutils-build elfutils-install
cd elfutils-build
../configure --prefix=`pwd`/../elfutils-install 
make && make install

#build and install boost

cd $root
cd pkgs-src
tar xvf boost_1_67_0.tar.bz2
cd boost_1_67_0
./bootstrap.sh
./b2  

#build and install dyninst
cd $root
export CPATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/include:`pwd`/pkgs-src/boost_1_67_0:$CPATH
export LD_LIBRARY_PATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/lib:`pwd`/pkgs-src/boost_1_67_0/stage/lib:$LD_LIBRARY_PATH
cd pkgs-src/dyninst
mkdir dyninst-build dyninst-install
cd dyninst-build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../dyninst-install -DBOOST_ROOT=`pwd`/../../boost_1_67_0 ..
make && make install

#build and install LLVM OpenMP runtime library 
cd $root
cd pkgs-src/llvm-openmp/openmp
mkdir llvm-openmp-build llvm-openmp-install 
cd llvm-openmp-build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++  -DCMAKE_INSTALL_PREFIX=`pwd`/../llvm-openmp-install ..    
make && make install

#build and install TCMalloc
cd $root
cd pkgs-src/gperftools
mkdir gperftools-build gperftools-install    
./autogen.sh
cd gperftools-build
../configure --prefix=`pwd`/../gperftools-install 
make && make install

#build and install romp
export CPATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/include:$CPATH
export LD_LIBRARY_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/lib:`pwd`/pkgs-src/gperftools/gperftools-install/lib:$LD_LIBRARY_PATH
cd $root
cd pkgs-src/romp-lib
mkdir romp-build romp-install
cd romp-build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../romp-install ..
make && make install

#build and install libdwarf
cd $root
cd pkgs-src
tar xvf libdwarf.tar.gz
cd libdwarf
mkdir libdwarf-build libdwarf-install
cd libdwarf-build
../configure --prefix=`pwd`/../libdwarf-install
make && make install

#build dyninst client
export CPATH=`pwd`/pkgs-src/libdwarf/libdwarf-install/include:`pwd`/pkgs-src/dyninst/dyninst-install/include:$CPATH
export LD_LIBRARY_PATH=`pwd`/pkgs-src/dyninst/dyninst-install/lib:`pwd`/pkgs-src/libdwarf/libdwarf-install/lib:$LD_LIBRARY_PATH
cd $root
cd pkgs-src/dyninst-client
make 


