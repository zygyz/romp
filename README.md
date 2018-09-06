# ROMP

ROMP is a dynamic data race detector for OpenMP programs. 

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. 

### System Requirements
1. Operating Systems:  Linux

2. Architecture:  x86_64

### Prerequisites

1. [Clang](https://github.com/llvm-mirror/clang) (version 8.0.0 or later versions)

2. [DynInst](https://github.com/dyninst/dyninst)

3. LLVM OpenMP runtime library (should use the llvm-openmp provided in romp/pkgs-src/)

4. TCMalloc(https://github.com/gperftools/gperftools) 

### Installing

ROMP relies on several packages. One need to install packages listed below

1. Clang version 8.0.0 or later version (required)
   - [Clang](https://github.com/llvm-mirror/clang) could be downloaded from [https://github.com/llvm-mirror/clang](https://github.com/llvm-mirror/clang)

2. DynInst (required)
   - DynInst relies on elfutils-0.173 and boost library 

   - to build and install elfutils-0.173, start from romp root directory: 
    
   ```
       cd pkgs-src/elfutils-0.173
       mkdir elfutils-build elfutils-install
       cd elfutils-build
       ../configure --prefix=`pwd`/../elfutils-install 
       make && make install
   ```
   - to build and install boost, start from romp root directory
   ```    
       cd pkgs-src
       tar xvf boost_1_67_0.tar.bz2
       cd boost_1_67_0
       ./bootstrap.sh
       ./b2  
   ```
   - to build and install dyninst, start from romp root directory 
    
   ```
       export CPATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/include:`pwd`/pkgs-src/boost_1_67_0:$CPATH
       export LD_LIBRARY_PATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/lib:$LD_LIBRARY_PATH
       export LD_LIBRARY_PATH=`pwd`/pkgs-src/boost_1_67_0/stage/lib:$LD_LIBRARY_PATH
       cd pkgs-src/dyninst
       mkdir dyninst-build dyninst-install
       cd dyninst-build
       cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../dyninst-install -DBOOST_ROOT=`pwd`/../../boost_1_67_0 ..
       make && make install
   ```  

3.  LLVM OpenMP runtime library (required)
  
  - to build and install LLVM OpenMP runtime library, start from romp root directory 
   
  ```
       cd pkgs-src/llvm-openmp/openmp
       mkdir llvm-openmp-build llvm-openmp-install 
       cd llvm-openmp-build
       cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++  -DCMAKE_INSTALL_PREFIX=`pwd`/../llvm-openmp-install ..    
       make && make install
  ```   

4. tcmalloc (required)
  - to build and install TCMalloc, start from romp root 
  ```
     cd pkgs-src/gperftools
     mkdir gperftools-build gperftools-install    
     ./autogen.sh
     cd gperftools-build
     ../configure --prefix=`pwd`/../gperftools-install 
     make && make install
  ``` 

5. ROMP 
  - to build and install ROMP, start from romp root
  ```
     export CPATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/include:$CPATH
     export LD_LIBRARY_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/lib:$LD_LIBRARY_PATH
     export LD_LIBRARY_PATH=`pwd`/pkgs-src/gperftools/gperftools-install/lib:$LD_LIBRARY_PATH
     cd pkgs-src/romp-lib
     mkdir romp-build romp-install
     cd romp-build
     cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../romp-install ..
     make && make install
  ```
6. DynInst Client 
  - DynInst relies on libdwarf, to build libdwarf, start from romp root
  ```
       cd pkgs-src
       tar xvf libdwarf.tar.gz
       cd libdwarf
       mkdir libdwarf-build libdwarf-install
       cd libdwarf-build
       ../configure --prefix=`pwd`/../libdwarf-install
       make && make install
  ``` 

  - to build dyninst client, start from romp root
  ```
     export CPATH=`pwd`/pkgs-src/libdwarf/libdwarf-install/include:$CPATH
     export CPATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/include:$CPATH
     export CPATH=`pwd`/pkgs-src/dyninst/dyninst-install/include:$CPATH
     export LD_LIBRARY_PATH=`pwd`/pkgs-src/dyninst/dyninst-install/lib:$LD_LIBRARY_PATH
     export LD_LIBRARY_PATH=`pwd`/pkgs-src/libdwarf/libdwarf-install/lib:$LD_LIBRARY_PATH
     export LD_LIBRARY_PATH=`pwd`/pkgs-src/elfutils-0.173/elfutils-install/lib:$LD_LIBRARY_PATH
     cd pkgs-src/dyninst-client
     make 
  ```
  - after compilation, one should get a binary called omp_race_client

### Trouble shooting for installation
   - When installing dyninst, one may encounter the following error:
   ```
     .../dyninst-build/elfutils/src/LibElf/backends/ppc_init_reg.c:69:1: error: stack usage might be unbounded
   ```
   To solve this problem, add the following line in ppc_initreg.c: 
   ```
        #pragma GCC diagnostic ignored "-Wstack-usage="
   ```
## Running the tests

### Running benchmarks
1. DataRaceBench
   - to run DataRaceBench, first make sure ROMP has been intalled as described above
   - start from romp root
   ```
      export DYNINST_ROOT=`pwd`/pkgs-src/dyninst/dyninst-install
      export DYNINSTAPI_RT_LIB=$DYNINST_ROOT/lib/libdyninstAPI_RT.so
      export DYNINST_CLIENT=`pwd`/pkgs-src/dyninst-client/omp_race_client
      export ROMP_PATH=`pwd`/pkgs-src/romp-lib/romp-install/lib/libomptrace.so
      export CPATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/include:$CPATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/lib:$LD_LIBRARY_PATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/gperftools/gperftools-install/lib:$LD_LIBRARY_PATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/dyninst/dyninst-install/lib:$LD_LIBRARY_PATH
      cd tests/dataracebench
      ./check-data-races.sh --romp
   ```

2. OmpSCR   
    - start from romp root 
    ```
      export DYNINST_ROOT=`pwd`/pkgs-src/dyninst/dyninst-install
      export DYNINSTAPI_RT_LIB=$DYNINST_ROOT/lib/libdyninstAPI_RT.so
      export DYNINST_CLIENT=`pwd`/pkgs-src/dyninst-client/omp_race_client
      export ROMP_PATH=`pwd`/pkgs-src/romp-lib/romp-install/lib/libomptrace.so
      export C_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/include:$CPATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/llvm-openmp/openmp/llvm-openmp-install/lib:$LD_LIBRARY_PATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/gperftools/gperftools-install/lib:$LD_LIBRARY_PATH
      export LD_LIBRARY_PATH=`pwd`/pkgs-src/dyninst/dyninst-install/lib:$LD_LIBRARY_PATH
      cd tests/OmpSCR_v2.0 
      gmake bashconfig
    ```
     - after configuration  
    ```
       gmake par     
    ```
     - to instrument an application to enable romp 
    ```
     $DYNINST_CLIENT your_application  
    ``` 
     - the instrumented binary is called instrumented_app

### Running custom tests 
   - to run your own tests, first compile your program, then: 
   ```
      export DYNINST_CLIENT=/path/to/dyninst/client/omp_race_client
      export ROMP_PATH=/path/to/libomptrace.so
      $DYNINST_CLIENT your_program
      mv instrumented_app renamed_instrumented_program
      ./renamed_instrumented_program [your parameters]
   ``` 

### Miscellaneous
    - to turn on verbose race report: 
       export ROMP_VERBOSE=on 
    - to turn off verboes race report:
       export ROMP_VERBOSE=off 

### Caveats
    - For DRB047 in dataracebench, please use the byte level granularity checking otherwise the word level granularity checking causes false positives 
    - To siwtch from word level checking to byte level checking, disable the macro definition #define WORD_LEVEL and enable the macro definition #define BYTE_LEVEL and recompile romp library
    - For DRB114 in dataracebench, whether data race occurs is dependent on the control flow, i.e., the value of rand()%2 
    - DRB094,DRB095,DRB096,DRB112 require an OpenMP 4.5 compiler, currently romp does not test these programs
    - DRB116 tests use of target + teams constructs. The support for these two constructs is out of the scope of our SC18 paper. Adding support for targe and teams constructs is subject to ongoing work.
## Authors

* **Yizi Gu (yg31@rice.edu)** - *Initial work* 

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details
