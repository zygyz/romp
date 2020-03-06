Refactoring and improving ROMP is in progress.

### ROMP 
A dynamic data race detector for OpenMP program 

### System Requirements
Operating Systems: Linux

Architecture: x86_64

Compiler: gcc 9.2.0 (recommended); c++17 support is required;

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
* (optional) create a symlink to the spack installed gcc location: 
 ```
 ln -s `spack location --install-dir gcc@9.2.0` /home/to/your/gcc/root
 ```
* update spack compiler configuration file 
  * `spack config edit compilers`
  * inside the opened file, set up another compiler configuration entry using 
  the path to the gcc 9.2.0 compiler. If you set up the symlink, it could be the symlink path. 
  * configuration guide for the compiler can be found in: 
    
    https://spack-tutorial.readthedocs.io/en/latest/tutorial_configuration.html
    
3. install ROMP
  ```
  spack install romp@develop^dyninst%10.1.2~openmp%gcc@9.2.0
  ```
##### Setup environment variables 
 Setup environment variables so that we can run ROMP:
 ```
 export DYNINST_PREFIX=`spack location --install-dir dyninst`
 export ROMP_PREFIX=`spack location --install-dir romp`
 export LLVM_PREFIX=`spack location --install-dir llvm-openmp`
 export LIBRARY_PATH=$LLVM_PREFIX/lib
 export LD_LIBRARY_PATH=$LLVM_PREFIX/lib:$DYNINST_PREFIX/lib
 export DYNINSTAPI_RT_LIB=$DYNINST_PREFIX/lib/libdyninstAPI_RT.so
 export ROMP_PATH=$ROMP_PREFIX/lib/libomptrace.so
 export PATH=$ROMP_PREFIX/bin:$PATH
 ```
#### Install ROMP using CMake
People may want a faster development and iteration experience when debugging and developing ROMP. Installation using 
spack requires changes to be committed to remote repos. ROMP's cmake files make it possible to build ROMP and a local copy of dyninst without using spack. Note that we still use spack to install some dependent libraries.

1. install `spack`
*  same as described in above section
2. install dependent packages
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
3. setup envorinment variables for building
  ```
   export GLOG_PREFIX=`spack location --install-dir glog`
   export GFLAGS_PREFIX=`spack location --install-dir gflags`
   export LLVM_PREFIX=`spack location --install-dir llvm-openmp`
   export BOOST_PREFIX=`spack location --install-dir boost`
   export DYNINST_PREFIX=`spack location --install-dir dyninst`
   export TBB_PREFIX=`spack location --install-dir intel-tbb`
   export CPLUS_INCLUDE_PATH=$GLOG_PREFIX/include:\
   $GFLAGS_PREFIX/include:$BOOST_PREFIX/include:\
   $DYNINST_PREFIX/include:$TBB_PREFIX/include:\
   $LLVM_PREFIX/include
   export LIBRARY_PATH=$GLOG_PREFIX/lib:$GFLAGS_PREFIX/lib:$LLVM_PREFIX/lib
   export LD_LIBRARY_PATH=$GLOG_PREFIX/lib:$GFLAGS_PREFIX/lib:\
                           $LLVM_PREFIX/lib:$DYNINST_PREFIX/lib

  ```
4. build and install romp
* suppose romp is located in `/home/to/romp`
  ```
   cd /home/to/romp
   mkdir build
   mkdir install
   cd build
         
   cmake -DCMAKE_PREFIX_PATH="$GFLAGS_PREFIX;$GLOG_PREFIX;$DYNINST_PREFIX;$BOOST_PREFIX"
         -DLLVM_PATH=$LLVM_PREFIX -DCMAKE_CXX_FLAGS=-std=c++17 
         -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc 
         -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
   make
   make install
  ```
##### Setup environment variables 
Setup environment variables so that we can run ROMP. 
* Suppose romp root path is `/home/to/romp`:
```
 export ROMP_PATH=/home/to/romp/install/lib/libomptrace.so
 export DYNINSTAPI_RT_LIB=$DYNINST_PREFIX/lib/libdyninstAPI_RT.so
 export LD_LIBRARY_PATH=$GLOG_PREFIX/lib:\
                        $LLVM_PREFIX/lib:\
                        $DYNINST_PREFIX/lib
 export PATH=/home/to/romp/install/bin:$PATH
```

### Compile and instrument a program
* suppose an OpenMP program is `test.cpp`
1. compile the program so that it links against our llvm-openmp library
```
g++ -g -fopenmp -lomp test.cpp -o test
```
* one can `ldd test` to check if `libomp` is our spack installed one, which contains changes to support OMPT callbacks
* if the linkage is incorrect, e.g., it uses system library, check if the library name mismatches:
```
cd `spack location --install-dir llvm-openmp`/lib
```
* it is possible that linker wants to find `libomp.so.5` but the spack installed lib only contains `libomp.so`. In this case, create a symlink `libomp.so.5->libomp.so` yourself

2. instrument the binary
* use dyninst instrument client `InstrumentMain` to instrument the binary
```
InstrumentMain --program=./test
```
* this would generate an instrumented binary: `test.inst`
3. check data races for a program
* (optional) turn on line info report.
```
export ROMP_REPORT_LINE=on
```
when enabled, this would print all data races found with line information
* (optional) turn on on-the-fly data race report
```
export ROMP_REPORT=on
```
when enabled, once a data race is found during the program execution, it is reported. Otherwise,
all report would be generated after the execution of the program
* run `test.inst` to check data races for program `test`

### Running DataRaceBench
* check out my forked branch `romp-test` of data race bench, which contains modifications to scripts to support running romp
 https://github.com/zygyz/dataracebench 
```
git clone git@github.com:zygyz/dataracebench.git
git checkout romp-test
cd dataracebench
./check-data-races.sh --romp
```
