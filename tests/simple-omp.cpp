#include <omp.h>

int main(int argc, const char* argv[]) {
  int x = 0;
  #pragma omp parallel
  {
    x++;
  }
  return 0;
}




