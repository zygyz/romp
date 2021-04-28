Refactoring and improving ROMP is in progress.

### ROMP 
A dynamic data race detector for OpenMP program 

### System Requirements
Operating Systems: Linux

Architecture: x86_64

Compiler: gcc supporting c++17

### Install ROMP

#### Install ROMP using Spack
**Currently installing ROMP using spack is not maintained. Please install ROMP using CMake. But please follow the steps in this section to install environment module etc.**

1. Install `spack`
* Checkout my forked branch of `spack`. It contains changes to package.py for `llvm-openmp`, `dyninst`, and 
the pacakge spec for `romp`:

```
   git clone https://github.com:zygyz/spack.git
   git checkout romp-build
```
* For the installation of Spack, please refer to the guide in Spack project readme. 

2. Install environment module
* We use environment module to manage environment variable settings. Section 'Bootstrapping Environment Modules' in link http://hpctoolkit.org/software-instructions.html#Building-a-New-Compiler provides a guide to installing environment module.

3. Setup spack configuration for environment modules
* Spack treats each software it manages as a module. A module contains package and modulefile. Package contains all the compiled binaries of the software. Modulefile is for environment module to setup the environment variables of the software such as LD_LIBRARY_PATH. Do the following steps to tell spack where to put packages and modulefiles. Usually packages and modulefiles are under the same directory. Suppose we use /path/to/spack/Modules as the directory:

* edit config.yaml in $HOME/.spack:

```
config:
  install_tree: /path/to/spack/Modules/packages
  module_roots:
    tcl: /path/to/spack/Modules/modules
```

* edit modules.yaml in $HOME/.spack:

```
modules:
   enable:
    - tcl 
```
One can replace `tcl` with `lmod` depending on which interpreter the system uses to manage modulefiles. 

3. Install gcc a.b.c
* Before installing using spack, it is important to make sure your system has a clean environment variable setting.
This can be done by `printenv | grep PATH`. Then:
```
spack install gcc@a.b.c
```
4. Using gcc a.b.c for all builds
* To ensure all packages are built using the same compiler, do the following steps:
  * After installation of gcc, you will find a directory in /path/to/spack/Modules/modules, suppose it is called system-arch. You can also find the gcc module in this direcotry, suppose it is called gcc-a.b.c-gcc-a.b.c-somehash Do the following steps:
```
module use /path/to/spack/Modules/modules/system-arch
module load gcc-a.b.c-gcc-e.f.g-somehash
```
  * Tell spack to add gcc a.b.c into available compilers:
```
spack compiler find
```
  * Tell spack to use gcc a.b.c to build the rest of all software by editing $HOME/.spack/packages.yaml:
```
packages:
  all:
    compiler: [gcc@a.b.c]
```

4. Install ROMP
  ```
  spack install romp@master
  ```

#### Install ROMP using CMake
Spack installation of ROMP requires changes to be committed to remote repos. ROMP's cmake files make it possible to build ROMP without using spack. Note that we still use spack to install some dependent libraries.

1. Install and configure `spack`
* Use the same steps as described above.

2. Install dependent packages
* glog
  ```
  spack install glog 
  ```
* llvm-openmp
  ```
  spack install llvm-openmp@romp-mod
  ```
* dyninst
  ```
  spack install dyninst@11.0.0~openmp
  ``` 
3. Setup environment varibales for building ROMP 
* We need environment variables setting for several dependent packages (gflags, intel-tbb, boost etc.). The exact name for each package can be found using `module avail` command. Please take a look at the env_setup.sh file for your reference. Remember to replace each module load commend with your system's settings in env_setup.sh

4. Build makefiles using cmake
* Suppose ROMP is located in `/home/to/romp`
  ```
   cd /home/to/romp
   source ./env_setup.sh
   ./install.sh
  ```

#### Install ROMP that uses local build of Dyninst 
1. Install and configure `spack`
* Use the same steps as described above.

2. Install dependent packages
* glog
  ```
  spack install glog 
  ```
* llvm-openmp
  ```
  spack install llvm-openmp@romp-mod
  ```
* dyninst
  Build your own dyninst using cmake locally. 

3. Setup environment varibales for building ROMP 
* We need environment variables setting for several dependent packages (gflags, intel-tbb, boost etc.). The exact name for each package can be found using `module avail` command. Please take a look at the env_setup.sh file for your reference. Remember to replace each module load command with your own system's settings.


4. Build makefiles using cmake
* Suppose ROMP is located in `/home/to/romp`, and dyninst is installed in `/home/to/dyninst/install`
  ```
   export CUSTOM_DYNINST_PREFIX=/home/to/dyninst/install
   export CPLUS_INCLUDE_PATH=/home/to/dyninst/install/include:$CPLUS_INCLUDE_PATH
   module load gcc-7.4.0-gcc-a.b.c-somehash
   cd /home/to/romp
   mkdir build
   mkdir install
   cd build
         
   cmake -DCMAKE_PREFIX_PATH="$CUSTOM_DYNINST_PREFIX"
         -DCMAKE_CXX_FLAGS=-std=c++17 
         -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
         -DCMAKE_BUILD_TYPE=RelWithDebInfo
         -DCUSTOM_DYNINST=ON
         -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..
   make
   make install
  ```

### Running ROMP 
#### Setup environment variables so that we can run ROMP. 
1. Load the following modules into environment variables. Remember to replace contents in env_setup.sh with your own system settings.
 ```
  source ./env_setup.sh
 ```
It is possible that various verions/variants of dyninst are installed in your system. For example, hpctoolkit requires a variant of dyninst that supports parallel parsing using OpenMP, while ROMP requires a variant of dyninst that turns off this parallel parsing feature. It is important to make sure the correct version/variant of dyninst is used by ROMP. To check this, one can run 
```
spack spec -l hpctoolkit
``` 

2. Export DYNINSTAPI_RT_LIB and ROMP_PATH
* It is required by dyninst to set environment variable DYNINSTAPI_RT_LIB. Make sure one uses the correct version of dyninst using the method described above. 

```
export DYNINSTAPI_RT_LIB=`spack location --install-dir dyninst/jmaisrn`/lib/libdyninstAPI_RT.so
```
ROMP's instrumentation client needs to know where ROMP library is located. This is done by setting environment variable ROMP_PATH. Depending on the ROMP installation method:
* If ROMP is installed using spack:
```
export ROMP_PATH=`spack location --install-dir romp`/lib/libromp.so
```
* If ROMP is installed using cmake:

```
export ROMP_PATH=/path/to/romp/install/lib/libromp.so
```
 
#### Compile and instrument a program
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
