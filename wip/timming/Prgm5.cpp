#include "time.h"
#include <iostream>

using namespace std;

void run(int topCandidate) {
  int candidate = 2;
  while(candidate <= topCandidate) {
    int trialDivisor = 2;
    int prime = 1;
	    
    bool found = true;
    while(((trialDivisor * trialDivisor) <= candidate) && found == true) {
      if(candidate % trialDivisor == 0) {
	prime = 0;
	found = false;
      }
      else {
	trialDivisor++;
      }
    }
	    
    if(found == true) {
      cout << candidate << endl;
    }
    candidate++;
  }
}

int main() {
  long start = clock();
	
  run(1000000);
  
  cout << "---------------------------" << endl;
  cout << "Time: " << (float)(clock() - start) / CLOCKS_PER_SEC << " second(s)." << endl;	  
}
