#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "common.h"

using namespace std;

enum Type {
  INT_LIT,
  INT_VAR,
  ADD_OPER,
  MUL_OPER,
  JUMP
};

class CodeElement {
  Type type;
  long value;
  string string_value;

  string LongToString(long v) {
    ostringstream str;
    str << v;
    return str.str();
  }
  
 public:
  CodeElement(Type t) {
    type = t;
    value = -1;
  }
  
  CodeElement(Type t, long v) {
    type = t;
    value = v;
  }
  
  ~CodeElement() {
  }
  
  // TODO: for value numbering
  const string& ToString() {
    if(string_value.size() == 0) {
      switch(type) {	
      case INT_LIT:
	string_value = LongToString(value);
	break;
      
      case INT_VAR:
	string_value = 'v' + LongToString(value);
	break;

      case ADD_OPER:
	string_value = '+';
	break;
      
      case MUL_OPER:
	string_value = '*';
	break;

      case JUMP:
	string_value = "jmp:" + LongToString(value);
	break;
      }
    }

    return string_value;
  }
};

class CodeSegment {
  CodeElement* result;
  CodeElement* left;
  CodeElement* oper;
  CodeElement* right;
  string string_value;
  
 public:
  CodeSegment(CodeElement* r, CodeElement* lhs) {
    result = r;
    left = lhs;
    oper = NULL;
    right = NULL;
  }

  CodeSegment(CodeElement* r, CodeElement* lhs, CodeElement* o, CodeElement* rhs) {
    result = r;
    left = lhs;
    oper = o;
    right = rhs;
  }

  ~CodeSegment() {
    if(result) {
      delete result;
      result = NULL;
    }
    
    if(left) {
      delete left;
      left = NULL;
    }
    
    if(oper) {
      delete oper;
      left = NULL;
    }

    if(right) {
      delete right;
      right = NULL;
    }
  }

  const string& ToString() {
    if(string_value.size() == 0) {
      string_value = result->ToString() + '=' + left->ToString();
      if(oper && right) {
	string_value += oper->ToString() + right->ToString();
      }
    }

    return string_value;
  }
  
  bool IsUnary() {
    return !oper && !right;
  }
};

class CodeBlock {
  vector<CodeSegment*> segments;
  vector<CodeBlock*> parents;
  vector<CodeBlock*> children;
 
 public:
  CodeBlock() {
  }

  ~CodeBlock() {
    while(!segments.empty()) {
      CodeSegment* tmp = segments.front();
      segments.erase(segments.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
    
    while(!parents.empty()) {
      CodeBlock* tmp = parents.front();
      parents.erase(parents.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!children.empty()) {
      CodeBlock* tmp = children.front();
      children.erase(children.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
  }

  void AddSegment(CodeSegment* s) {
    segments.push_back(s);
  }
  
  void AddParent(CodeBlock* p) {
    parents.push_back(p);
  }

  void AddChild(CodeBlock* c) {
    children.push_back(c);
  }

  void Print() {
    // print current block
    for(int i = 0; i < segments.size(); i++) {
      cout << segments[i]->ToString() << endl;
    }
    // print childern
    cout << "---------" << endl;
    for(int i = 0; i < children.size(); i++) {
      children[i]->Print();
    }
  }
};

class Optimizer {
  CodeBlock* root;
  
 public:
  Optimizer() {
  }

  ~Optimizer() {
    if(root) {
      delete root;
      root = NULL;
    }
  }

  void Print() {
    root->Print();
  }
  
  void Optimize();
};

#endif
