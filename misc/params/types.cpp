#include <assert.h>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main() {
  vector<string> method_names;
  method_names.push_back("Struct.IntVector:MergeSort:i,i,i**,i*,");
  method_names.push_back("Struct.IntVector:Apply:m.(i,)~i,");
  method_names.push_back("Struct.FloatVector:Size:");
  method_names.push_back("Net.TCPSocket:New:o.System.String,i,");
  method_names.push_back("Net.TCPSocket:IsOpen:");
	
  for(size_t i = 0; i < method_names.size(); i++) {
    const string &method_name = method_names[i];
    size_t start = method_name.find_last_of(':'); 	
    if(start != string::npos) {
      cout << "method=|" << method_name << "| parms[";
      const string &parameters = method_name.substr(start + 1);
      size_t index = 0;

      while(index < parameters.size()) {
	int dimension = 0;
	switch(parameters[index]) {
	case 'l':
	case 'b':
	case 'i':
	case 'f':
	case 'c':
	case 'n':
	  cout << "basic";
	  index++;
	  break;		

	case 'm': {
     index += 2;
	  start = index;
	  while(index < parameters.size() && parameters[index] != '~') {
	    index++;
	  }
	  index++;
	  while(index < parameters.size() && parameters[index] != '*' && 
		parameters[index] != ',') {
	    index++;
	  }	
	  size_t end = index;
	  const string &name = parameters.substr(start, end - start);
	  cout << "method: name=|" << name << "|";
	}
	  break;

	case 'o': {
	  index += 2;
	  start = index;
	  while(index < parameters.size() && parameters[index] != '*' && 
		parameters[index] != ',') {
	    index++;
	  }	
	  size_t end = index;
	  const string &name = parameters.substr(start, end - start);
	  cout << "class: name=|" << name << "|";
	}
	  break;
	}

	while(index < parameters.size() && parameters[index] == '*') {
	  dimension++;
	  index++;
	}
	cout << " dim=" << dimension;
	
	assert(parameters[index] == ',');
	index++;
	
	cout << "; ";
      }
      cout << "]" << endl;
    }
  }

  return 0;
}

