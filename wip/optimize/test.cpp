#include "optimizer.h"

int main() {
  Optimizer optimize;
  optimize.LoadSegments();
  optimize.Print();
  
  optimize.Optimize();
  optimize.PrintOptimized();
  
  return 0;
}
