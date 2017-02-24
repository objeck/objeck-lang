#ifndef __INTERACTIVE_H__
#define __INTERACTIVE_H__

#define SPACES " \t\r\n"

#include <windows.h>
#include <iostream>
#include <string>
#include <list>
#include <map>

using namespace std;

typedef int (*Compile)(map<const string, string>, const string);

string trim_right(const string & s, const string & t = SPACES)
{
    string d(s);
    string::size_type i(d.find_last_not_of(t));
    if(i == string::npos)
        return "";
    else
        return d.erase(d.find_last_not_of(t) + 1) ;
}  

string trim_left(const string & s, const string & t = SPACES)
{
    string d(s);
    return d.erase(0, s.find_first_not_of(t)) ;
}  

string trim(const string & s, const string & t = SPACES)
{
    string d(s);
    return trim_left(trim_right(d, t), t) ;
} 

class SourceMethod {
  string alias;
  list<string> lines;

public:
  SourceMethod(string a, string d) {
		alias = a;
    lines.push_back(d);
    lines.push_back("}");
  }
  
	string GetAlias() {
		return alias;
	}
                                  
  string GetLine(unsigned int pos) {
    if(pos < 1 || pos > lines.size() - 1) {
      return false;
    }

    list<string>::iterator start = lines.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }

    return (*start);
  }

  bool DeleteLine(unsigned int pos) {
    if(pos < 1 || pos > lines.size() - 1) {
      return false;
    }

    list<string>::iterator start = lines.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }
    lines.erase(start);
    return true;
  }

  bool InsertLine(string line, unsigned int pos) {
    if(pos < 1 || pos > lines.size() - 1) {
      return false;
    }
    
    list<string>::iterator start = lines.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }
    lines.insert(start, line);
    return true;
  }
  
  void AddLine(string line) {
    list<string>::iterator start = lines.begin();
    for(int i = 0; i < lines.size() - 1; i++) {
      start++;
    }
    string formatted_line = "\t" + line;
    lines.insert(start, formatted_line);
  }

  void ListLines() {
    int i = 0;
    list<string>::iterator iter = lines.begin();
    for(; iter != lines.end(); iter++ ) {
				cout << (++i) << ": " << (*iter) << endl;
	  }
  }

  list<string> GetLines() {
    return lines;
  }

  void Clear() {
    lines.clear();
  }
};

class SourceProgram {
  map<string, SourceMethod*> methods;

public:
  void AddMethod(SourceMethod* m) {
    methods.insert(pair<string, SourceMethod*>(m->GetAlias(), m));
  }

  SourceMethod* GetMethod(string a) {
     map<string, SourceMethod*>::iterator found = methods.find(a);
     if(found != methods.end()) {
       return found->second;
     }

     return NULL;
  }

  void ListMethods() {
    cout << "methods:" << endl;
    map<string, SourceMethod*>::iterator iter = methods.begin();
    for(; iter != methods.end(); iter++) {
      cout << "  " << iter->second->GetAlias() << endl;
    }
  }

  bool CompileProgram() {
    int status;
    HINSTANCE compiler_lib = LoadLibrary("obc.dll");
    if(compiler_lib) {
      Compile _Compile = (Compile)GetProcAddress(compiler_lib, "Compile");
      if(_Compile) {
        map<const string, string> arguments;
        arguments["src"] = "_interactive_.obs";
        arguments["lib"] = "lang.obl";
        arguments["dest"] = "_interactive_.obl";
        const string usage = "...";
        int status = _Compile(arguments, usage);
        return true;
      }
    }
    
    return false;
  }
  
  string ToString() {
    string source = "bundle Default {\n";
	  source += "\tclass Interactive {\n";
    
    map<string, SourceMethod*>::iterator iter = methods.begin();
    for(; iter != methods.end(); iter++) {
      // get lines
      list<string> lines = iter->second->GetLines();
      list<string>::iterator line = lines.begin();
      for(; line != lines.end(); line++ ) {
        string formatted_line = "\t\t" + (*line) + "\n";
        source += formatted_line;
      }
    }
    source += "\t}\n";
    source += "}\n";

    return source;
  }
};

#endif
