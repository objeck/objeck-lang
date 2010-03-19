#ifndef __INTERACTIVE_H__
#define __INTERACTIVE_H__

#define SPACES " \t\r\n"

#include <iostream>
#include <string>
#include <vector>
#include <list>

using namespace std;

class SourceMethod {
  list<string> method;

public:
  SourceMethod(string declaration) {
    method.push_back(declaration);
    method.push_back("}");
  }

  string GetLine(int pos) {
    if(pos < 1 || pos > method.size() - 1) {
      return false;
    }

    list<string>::iterator start = method.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }

    return (*start);
  }

  bool DeleteLine(int pos) {
    if(pos < 1 || pos > method.size() - 1) {
      return false;
    }

    list<string>::iterator start = method.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }
    method.erase(start);
    return true;
  }

  bool InsertLine(string line, int pos) {
    if(pos < 1 || pos > method.size() - 1) {
      return false;
    }
    
    list<string>::iterator start = method.begin();
    for(int i = 0; i < pos; i++) {
      start++;
    }
    method.insert(start, line);
    return true;
  }
  
  void AddLine(string line) {
    list<string>::iterator start = method.begin();
    for(int i = 0; i < method.size() - 1; i++) {
      start++;
    }
    method.insert(start, line);
  }

  void List() {
    list<string>::iterator iter = method.begin();

    int i = 0;
    for(; iter != method.end(); iter++ ) {
				cout << (++i) << ": " << (*iter) << endl;
			}
  }

  void Clear() {
    method.clear();
  }
};

string trim_right (const string & s, const string & t = SPACES)
{
    string d (s);
    string::size_type i (d.find_last_not_of (t));
    if (i == string::npos)
        return "";
    else
        return d.erase (d.find_last_not_of (t) + 1) ;
}  

string trim_left (const string & s, const string & t = SPACES)
{
    string d (s);
    return d.erase (0, s.find_first_not_of (t)) ;
}  

string trim (const string & s, const string & t = SPACES)
{
    string d (s);
    return trim_left (trim_right (d, t), t) ;
}  

#endif
