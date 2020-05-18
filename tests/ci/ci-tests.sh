#!/usr/bin/env bash
CSV_HEADER="tool, id, filename, haverace, threads, dataset, races, elapsed-time(seconds), used-mem(KBs), compile-return, runtime-return"
TESTS=($(grep -l main ./benchmarks/*.cpp ./benchmarks/*.c))
OUTPUT_DIR="results"
LOG_DIR="$OUTPUT_DIR/log"
EXEC_DIR="$OUTPUT_DIR/exec"
MEMCHECK=${MEMCHECK:-"/usr/bin/time"}
TIMEOUTCMD=${TIMEOUTCMD:-"timeout"}
ROMP_CPP_COMPILE_FLAGS="-g -std=c++11 -fopenmp -lomp"
ROMP_C_COMPILE_FLAGS="-g -fopenmp -lomp"

POLYFLAG="benchmarks/utilities/polybench.c -I benchmarks -I benchmarks/utilities -DPOLYBENCH_NO_FLUSH_CACHE -DPOLYBENCH_TIME -D_POSIX_C_SOURCE=200112L"

VARLEN_PATTERN='[[:alnum:]]+-var-[[:alnum:]]+\.c'
RACES_PATTERN='[[:alnum:]]+-[[:alnum:]]+-yes\.c'
CPP_PATTERN='[[:alnum:]]+\.cpp'

check_return_code () {
  case "$1" in
    11) echo "Seg Fault"; testreturn=11 ;;
    124) echo "Executime timeout";testreturn=124  ;;
    139) echo "Seg Fault"; testreturn=11 ;;
    *) testreturn=0  ;;
  esac
}

mkdir -p "$OUTPUT_DIR"
mkdir -p "$LOG_DIR"
mkdir -p "$EXEC_DIR"

DATASET_SIZES=('32' '64' '128' '256' '512' '1024')
THREADLIST=('2' '3' '8' '12' '24')
ITERATIONS=2
TIMEOUTMIN="10"

THREAD_INDEX=0
SIZE_INDEX=0
ITER=1

ULIMITS=$(ulimit -s)
ulimit -s unlimited

MEMLOG="$LOG_DIR/romp.memlog"
file="$OUTPUT_DIR/romp.csv"
echo "Saving to: $file and $MEMLOG"
[ -e "$file" ] && rm "$file"
echo "$CSV_HEADER" >> "$file"

TEST_INDEX=0

# load modules 
module load gcc-7.4.0-gcc-7.5.0-domzzsx
module load llvm-openmp-romp-mod-gcc-7.4.0-6kbf57l 
module load glog-0.3.5-gcc-7.4.0-y7ajvq2  
module load dyninst-10.1.2-gcc-7.4.0-pxqjj4q
module load gflags-2.1.2-gcc-7.4.0-4vasfdn

DYNINSTAPI_RT_PATH=`spack location --install-dir dyninst/pxqjj4q`/lib/libdyninstAPI_RT.so
ROMP_PATH=../../install/lib/libromp.so
INST_CLIENT=../../install/bin/InstrumentMain

for test in "${TESTS[@]}"; do
  additional_compile_flags=''
  if [[ "$test" =~ $RACES_PATTERN ]]; then haverace=true; else haverace=false; fi
  if [[ "$test" =~ $VARLEN_PATTERN ]]; then SIZES=("${DATASET_SIZES[@]}"); else SIZES=(''); fi
  testname=$(basename $test)
  id=${testname#CI}
  id=${id%%-*}
  echo "$test has $testname and ID=$id"
  exname="$EXEC_DIR/$(basename "$test").out"
  rompexec="$exname.inst"  
  logname="$(basename "$test").log"
  if [[ -e "$LOG_DIR/$logname" ]]; then rm "$LOG_DIR/$logname"; fi
  if grep -q 'PolyBench' "$test"; then additional_compile_flags+=" $POLYFLAG"; fi
  
  if [[ "$test" =~ $CPP_PATTERN ]]; then
    echo "testing C++ code:$test"
    g++ $ROMP_CPP_COMPILE_FLAGS $additional_compile_flags $test -o $exname -lm;
    echo $exname
    $INST_CLIENT --program=$exname;
  else
    echo "testing C code:$test"
    gcc $ROMP_C_COMPILE_FLAGS $additional_compile_flags $test -o $exname -lm;
    echo $exname
    $INST_CLIENT --program=$exname;
  fi
  compilereturn=$?;
  echo "compile return code: $compilereturn"; 
  THREAD_INDEX=0
  for thread in "${THREADLIST[@]}"; do
    echo "Testing $test: with $thread threads"
    export OMP_NUM_THREADS=$thread
    SIZE_INDEX=0
    for size in "${SIZES[@]}"; do
      # Sanity check
      if [[ ! -e "$exname" ]]; then
        echo "romp,$id,\"$testname\",$haverace,$thread,${size:-"N/A"},,,,$compilereturn," >> "$file";
        echo "Executable for $testname with $thread threads and input size $size is not available" >> "$LOGFILE";
      elif { "./$exname $size"; } 2>&1 | grep -Eq 'Segmentation fault'; then
        echo "romp,$id,\"$testname\",$haverace,$thread,${size:-"N/A"},,,,$compilereturn," >> "$file";
        echo "Seg fault found in $testname with $thread threads and input size $size" >> "$LOGFILE";
      else
        ITER_INDEX=1
        for ITER in $(seq 1 "$ITERATIONS"); do
          echo -e "*****     Log $ITER_INDEX for $testname with $thread threads and input size $size     *****" >> "$LOG_DIR/$logname"
          start=$(date +%s%6N)
          $TIMEOUTCMD $TIMEOUTMIN"m" $MEMCHECK -f "%M" -o "$MEMLOG" "./$rompexec" $size &> tmp.log;
          check_return_code $?;
	  echo "test run return $testreturn"
          races=$(grep -ce 'data race found:' tmp.log) 
          end=$(date +%s%6N)
          elapsedtime=$(echo "scale=3; ($end-$start)/1000000"|bc)
          mem=$(cat $MEMLOG)
	  echo "thread: " $thread
          echo "romp,$id,\"$testname\",$haverace,$thread,${size:-"N/A"},${races:-0},$elapsedtime,$mem,$compilereturn,$testreturn" >> "$file"
	  if [[ $races -eq 0  &&  $haverace ]]; then
            echo "false negative on \"$testname\"" 
            exit 1
          fi
	  if [[ $races -eq 1 &&  ! $haverace ]]; then
            echo "false positive on \"$testname\""
	    exit 1
          fi
          ITER_INDEX=$((ITER_INDEX+1))
	done
      fi
    done
    THREAD_INDEX=$((THREAD_INDEX+1))
  done
  TEST_INDEX=$((TEST_INDEX+1))
done

ulimit -s "$ULIMITS"
