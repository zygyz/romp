# ROMP

ROMP is a dynamic data race detector for OpenMP programs. 

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. 

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
   - DynInst relies on elfutils-0.173
   - to build and install elfutils-0.173, start from romp root directory: 
    
   ```
       cd pkgs-src/elfutils-0.173
       mkdir elfutils-build elfutils-install
       cd elfutils-build
       ../configure --prefix=`pwd`/../elfutils-install 
       make && make install
   ```
  - to build and install dyninst, start from romp root directory 
    
   ```
       cd pkgs-src/dyninst
       mkdir dyninst-build dyninst-install
       cd dyninst-build
       cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../dyninst-install ..
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
     cd pkgs-src/gperftool
     mkdir gperftool-build gperftool-install    
     cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../gperftool-install ..
     make && make install
  ``` 

5. ROMP 
  - to build and install ROMP, start from romp root
  ```
     cd pkgs-src/romp-lib
     mkdir romp-build romp-install
     cd romp-build
     cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../romp-install ..
     make && make install
```
6. DynInst Client 
  - to build dyninst clinet, start from romp root
  ```
     cd pkgs-src/dyninst-client
     make 
  ```
  - after compilation, one should get a binary called omp_race_client

## Running the tests

### Running benchmarks
1. DataRaceBench
   - to run DataRaceBench, first make sure ROMP has been intalled as described above
   - start from romp root
   ```
      export DYNINST_CLIENT=/path/to/dyninst/client/omp_race_client
      export ROMP_PATH=/path/to/libomptrace.so
      cd tests/dataracebench
      ./check-data-races.sh --romp
   ```

2. OmpSCR   
    - start from romp root 
    ```
      export DYNINST_CLIENT=/path/to/dyninst/client/omp_race_client
      export ROMP_PATH=/path/to/libomptrace.so
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


## Authors

* **Yizi Gu (yg31@rice.edu)** - *Initial work* 

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE) file for details
