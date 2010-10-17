#include <iostream>

using namespace std;

int main() {
	int maxes[] = {3,5,2};
//	int maxes[] = {2,5,3};
	int indices[3];

	for(int i = 0; i < 3; i++) {			
		for(int j = 0; j < 5; j++) {			
			for(int k = 0; k < 2; k++) {			
				indices[0] = i;
				indices[1] = j;
				indices[2] = k;

				int dim = 3;

				int i = dim - 1;
				int index = indices[i];

				for(--i; i > -1; i--) { 
					index *= maxes[i];
					index += indices[i];  
				}

				cout << index << endl;
			}
		}
	}
}
