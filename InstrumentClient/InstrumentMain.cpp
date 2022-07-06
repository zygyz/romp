#include <cstdlib>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "InstrumentClient.h"

using namespace Dyninst;
using namespace romp;
using namespace std;

DEFINE_string(program, "", "program to be instrumented");
//DEFINE_string(source, "", "program source file");
DEFINE_string(arch, "x86", "arch of the binary to be instrumented");
DEFINE_string(modSuffix, ".inst", "suffix for name of instrumented binary");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  FLAGS_alsologtostderr = 1;
  google::InitGoogleLogging(argv[0]);
  if (FLAGS_program == "") {
    LOG(FATAL) << "no program name specified";
  } 
  auto envRompPath = getenv("ROMP_PATH");
  if (!envRompPath) {
    LOG(FATAL) << "ROMP_PATH env var is not set";
  }  
  auto bpatchPtr = make_shared<BPatch>(); 
  unique_ptr<InstrumentClient> client(
     new InstrumentClient(//FLAGS_source,
                          FLAGS_program, 
                          string(envRompPath), 
                          bpatchPtr, 
                          FLAGS_arch,
                          FLAGS_modSuffix));
  client->instrumentMemoryAccess();
  return 0;
}
