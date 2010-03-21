#include "interactive.h"

int main() {
  cout << "Welcome to Objeck! v0.9.7" << endl;
  
  
  SourceMethod* method = new SourceMethod("Main", "function : Main(args : String[]), Nil {");

	string line;
  bool quit = false;
	do {
		cout << "[" << method->GetAlias() << "]#> ";
		getline(cin, line);
    line = trim(line);

    // clear program
		if(line == "clear" || line == "c") {
			method->Clear();
			cout << "method cleared!" << endl;
		}
    // list program
		else if(line == "list" || line == "l") {
      method->List();
		}
    // insert line
		else if(line == "insert" || line == "i") {
			cout << "  line #? ";
      getline(cin, line);
      line = trim(line);
      int unsigned index = atoi(line.c_str());

      cout << "  insert? '";
      getline(cin, line);
      line = trim(line);
      
      if(!method->InsertLine(line, index - 1)) {
        cout << "!invalid line number" << endl;
      }
		}
    // replace line
		else if(line == "replace" || line == "r") {
			cout << "  line #? ";
      getline(cin, line);
      line = trim(line);
      int unsigned index = atoi(line.c_str());

      cout << "  replace? '";
      getline(cin, line);
      line = trim(line);
      
      if(!method->DeleteLine(index - 1)) {
        cout << "!invalid line number" << endl;
      }
      else {
        method->InsertLine(line, index - 1);
      }
		}
    // replace line
		else if(line == "delete" || line == "d") {
			cout << "  line #? ";
      getline(cin, line);
      line = trim(line);
      int unsigned index = atoi(line.c_str());

      if(!method->DeleteLine(index - 1)) {
        cout << "!invalid line number" << endl;
      }
    }
    // add line to method
    else if(line.size() > 0 && line[0] == '\'') {
      method->AddLine(line.substr(1));
    }
    // load library
		else if(line == "load lib" || line == "ll") {
      cout << "TODO: load lib" << endl;
    }
    // execute a program
		else if(line == "execute" || line == "x") {
      cout << "TODO: execute" << endl;
    }
    // quit session
		else if(line == "quit" || line == "q") {
			quit = true;
		}
    // change current function/method
		else if(line == "change method" || line == "cm") {
      cout << "  method/function? ";
      getline(cin, line);
      line = trim(line);

      cout << "now editing: " << line << "..." << endl;
		}
    // change current function/method
		else if(line == "new method" || line == "nm") {
			cout << "  alias? ";
      getline(cin, line);
      line = trim(line);

      cout << "  declaration? ";
      getline(cin, line);
      line = trim(line);

      cout << "now editing: " << line << "..." << endl;
		}
    // invalid command
    else {
      cout << "!invalid command" << endl;
    }
	}
	while(!quit);
	cout << "Goodbye..." << endl;
}
