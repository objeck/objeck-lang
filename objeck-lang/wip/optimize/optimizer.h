#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "common.h"

using namespace std;

class CodeElement;

enum Type {
  INT_LIT,
  INT_VAR,
  ADD_OPER,
  MUL_OPER,
  JUMP
};

static string LongToString(long v) {
  ostringstream str;
  str << v;
  return str.str();
}

class CodeElementVersion {
  Type type;
  long value;
  int version;
  string key;
  CodeElement* element;
  
 public:
  CodeElementVersion(Type t, long v, const string &k, int i, CodeElement* e) {
    type = t;
    value = v;
    key = k;
    version = i;
    element = e;
  }

  ~CodeElementVersion() {
  }
  
  const string GetKey() {
    if(type == INT_VAR) {
      return key + '(' + LongToString(version) + ')';
    }
    
    return key;
  }
  
  CodeElement* GetCodeElement() {
    return element;
  }
};

class CodeElement {
  Type type;
  long value;
  string key;
  int version_num;
  
 public:
  CodeElement(Type t) {
    type = t;
    value = -1;
    version_num = -1;
  }
  
  CodeElement(Type t, long v, const string& k) {
    type = t;
    value = v;
    key = k;
    version_num = -1;
  }
  
  ~CodeElement() {
  }

  // TODO: reutrn same version if no changes
  CodeElementVersion* GetVersion() {
    return new CodeElementVersion(type, value, key, version_num, this);
  }
  
  CodeElementVersion* GetNewVersion() {
    return new CodeElementVersion(type, value, key, ++version_num, this);
  }
  
  const string& GetKey() {
    return key;
  }
};

class CodeElementFactory {
  static CodeElementFactory* instance;
  map<const string, CodeElement*> elements;
  
  CodeElementFactory() {
  }

  ~CodeElementFactory() {
    // clean up
    map<const string, CodeElement*>::iterator iter;
    for(iter = elements.begin(); iter != elements.end(); iter++) {
      CodeElement* tmp = iter->second;
      delete tmp;
      tmp = NULL;
    }
    elements.clear();
  }

  // TODO: constants in blocks
  const string HashKey(Type t, long v) {
    string key;
    
    if(key.size() == 0) {
      switch(t) {	
      case INT_LIT:
	key = LongToString(v);
	break;
      
      case INT_VAR:
	key = 'v' + LongToString(v);
	break;

      case ADD_OPER:
	key = '+';
	break;
      
      case MUL_OPER:
	key = '*';
	break;

      case JUMP:
	key = "jmp:" + LongToString(v);
	break;
      }
    }

    return key;
  }

 public:
  static CodeElementFactory* Instance();

  CodeElement* MakeCodeElement(Type t) {
    return MakeCodeElement(t, -1);
  }
  
  bool HasCodeElement(CodeElement* e) {
    map<const string, CodeElement*>::iterator iter;
    for(iter = elements.begin(); iter != elements.end(); iter++) {
      if(iter->second == e) {
	return true;
      }
    }

    return false;
  }
  
  CodeElement* MakeCodeElement(Type t, long v) {
    const string& key = HashKey(t, v);
    map<const string, CodeElement*>::iterator result = elements.find(key);
    if(result != elements.end()) {
      return result->second;
    }
    else {
      CodeElement* element = new CodeElement(t, v, key);
      elements.insert(pair<const string, CodeElement*>(key, element));
      return element;
    }
  }
};

class CodeSegment {
  CodeElementVersion* result;
  CodeElementVersion* left;
  CodeElement* oper;
  CodeElementVersion* right;
  string key;
  
 public:
  CodeSegment(CodeElement* r, CodeElement* lhs) {
    result = r->GetNewVersion();
    left = lhs->GetVersion();
    oper = NULL;
    right = NULL;
  }
  
  CodeSegment(CodeElement* r, CodeElement* lhs, CodeElement* o, CodeElement* rhs) {
    result = r->GetNewVersion();
    left = lhs->GetVersion();
    oper = o;
    right = rhs->GetVersion();
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
    
    if(right) {
      delete right;
      right = NULL;
    }
  }
  
  const string& GetKey() {
    if(key.size() == 0) {
      key = result->GetKey() + '=' + left->GetKey();
      if(oper && right) {
	key += oper->GetKey() + right->GetKey();
      }
    }

    return key;
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
      cout << segments[i]->GetKey() << endl;
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
