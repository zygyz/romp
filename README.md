Refactoring and improving ROMP is in progress. Please refer to romp-v2 experimental branch for the latest status. 
https://github.com/zygyz/romp-v2

### ROMP 
A dynamic data race detector for OpenMP program 

### System Requirements
Operating Systems: Linux

Architecture: x86_64

Compiler: gcc 9.2.0 (recommended); any compiler supporting c++17

### Installation Guide

By default, we use `spack` to manage the installation of ROMP and related packages.
This should be the default way of installing ROMP. Installation using CMake 'manually' 
is described in a separate section.

#### Install ROMP using Spack

1. install `spack`
* Checkout my forked branch of `spack`. It contains changes to package.py for `llvm-openmp`, `dyninst`, and 
the pacakge spec for `romp`:

```
   git clone git@github.com:zygyz/spack.git
   git checkout romp-build
```
* For the installation of Spack, please refer to the guide in Spack project readme. 

2. install gcc 9.2.0
* fetch and install gcc 9.2.0 using spack 
 ``` spack install gcc@9.2.0```
* (optional) create a symlink to the sapck installed gcc location: 
 ```
 ln -s `spack location install-dir gcc@9.2.0` /home/to/your/gcc/root
 ```
* update spack compiler configuration file 
  * `spack config edit compilers`
  * inside the opened file, set up another compiler configuration entry using 
  the path to the gcc 9.2.0 compiler. If you set up the symlink, it could be the symlink path. 
  
3. install dependent pacakges 
* gflags
  ``` 
  spack install gflags %gcc@9.2.0
  ```
* glog
  ```
  spack install glog %gcc@9.2.0
  ```
* llvm-openmp
  ```
  spack install llvm-openmp@romp-mod%gcc@9.2.0
  ``` 
* dyninst
  ```
  spack install dyninst@10.1.2%gcc@9.2.0
  ```
  
#### Install ROMP using CMake
People may want a faster developement and iteration experience when debugging and developing ROMP. Installation using 
spack requires changes to be committed to remote repos. ROMP's cmake files make it possible to build ROMP locally without
going through spack pipeline. Note that we still use spack to install some dependent libraries:

* gflags 
* glog 
* llvm-openmp

The installation of these libraries are described in the section above: 'Install ROMP using Spack'

We assume the two following things:


Remember to install with c++17 compatible compiler option. e.g.,

spack install gflags %gcc@9.2.0
Configure the compiler option by following the steps in

https://spack-tutorial.readthedocs.io/en/latest/tutorial_configuration.html

spack config edit compilers 
Build dyninst. Suppose the dyninst is located in /path/to/dyninst, and the artifact is installed in path/to/dyninst/install. Create a symlink: ln -s /path/to/dyninst/install $HOME/dyninst

set environement variables

export GLOG_PREFIX=`spack location --install-dir glog`
export GFLAGS_PREFIX=`spack location --install-dir gflags`
export LLVM_PREFIX=`spack location --install-dir llvm-openmp`
export CUSTOM_DYNINST_PREFIX=$HOME/dyninst
export LIBRARY_PATH=`spack location --install-dir glog`/lib\
`spack location --install-dir llvm-openmp`/lib
Change directory to romp-v2:
   mkdir install
   cd build
   cmake -DCMAKE_PREFIX_PATH="$GFLAGS_PREFIX;$GLOG_PREFIX;$CUSTOM_DYNINST_PREFIX"
         -DLLVM_PATH=$LLVM_PREFIX -DCMAKE_CXX_FLAGS=-std=c++17 -DCUSTOM_DYNINST=ON 
         -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
   make
   make install
Now dyninst client InstrumentMain is installed in romp-v2/install/bin Before running instrumentation, set up several environment variables:
 export ROMP_PATH=/path/to/romp-v2/install/lib/libomptrace.so
 export DYNINSTAPI_RT_LIB=$HOME/dyninst/lib/libdyninstAPI_RT.so
 export LD_LIBRARY_PATH=`spack location --install-dir glog`/lib:\
                        `spack location --install-dir llvm-openmp`/lib:\
                         $HOME/dyninst/lib
Now compile tests/test_lib_inst.cpp with:
g++ test.cpp -std=c++11 -lomp- fopenmp
Then, go to romp-v2/install/bin,
./InstrumentMain --program=./a.out
This will generate a.out.inst

LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH ./a.out.inst
The dyninst client code is in InstrumentClient. Core functions are in InstrumentClient.cpp. Library names are listed in skipLibraryName vector. Currently, three libraries could be instrumented and linked without generating segmentation fault: libomp.so, libgromp.so.1, libm.so.6.
