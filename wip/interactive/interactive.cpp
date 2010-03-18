#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main() {
	cout << "Welcome to Objeck! v0.9.7" << endl;

	vector<string> source;
	string line;
	do {	
		cout << "<#> ";
		getline(cin, line);

		if(line == "clear" || line == "c") {
			source.clear();
			cout << "program cleared!" << endl;
		}
		else if(line == "list" || line == "l") {
			for(int i = 0; i < source.size(); i++ ) {
				cout << i << ": " << source[i] << endl;
			}
		}
		else {
			source.push_back(line);
		}	
	}
	while(line != "quit" && line != "q"); 
	cout << "Goodbye!" << endl;
}
