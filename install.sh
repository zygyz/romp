#!/bin/bash

root=`pwd`

#build and install TCMalloc
cd pkgs-src
git clone https://github.com/gperftools/gperftools.git
cd gperftools
mkdir gperftools-build gperftools-install    
./autogen.sh
./autogen.sh
cd gperftools-build
../configure --prefix=`pwd`/../gperftools-install 
make && make install

#build and install dyninst
cd $root
cd pkgs-src
git clone https://github.com/dyninst/dyninst.git
cd dyninst
mkdir dyninst-build dyninst-install
cd dyninst-build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../dyninst-install -DBOOST_MIN_VERSION=1.61.0 -DLIBELF_INCLUDE_DIR="" -DLIBELF_LIBRARIES="" -DLIBDWARF_LIBRARIES="" -DLIBDWARF_INCLUDE_DIR="" ..
make -j4
make install

#build and install LLVM OpenMP runtime library 
cd $root
cd pkgs-src/llvm-openmp/openmp
mkdir llvm-openmp-build llvm-openmp-install 
cd llvm-openmp-build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++  -DCMAKE_INSTALL_PREFIX=`pwd`/../llvm-openmp-install ..    
make && make install


#build and install romp
cd $root
export CPATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/include:$CPATH
export LD_LIBRARY_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/lib:`pwd`/pkgs-src/gperftools/gperftools-install/lib:$LD_LIBRARY_PATH
cd pkgs-src/romp-lib
mkdir romp-build romp-install
cd romp-build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../romp-install ..
make && make install

#build dyninst client
cd $root
cd pkgs-src/dyninst-client
make 
