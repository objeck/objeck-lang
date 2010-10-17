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

  Type GetType() {
    return type;
  }
  
  const string GetKey() {
    if(type == INT_VAR) {
      return key + '(' + LongToString(version) + ')';
    }
    
    return key;
  }

  long GetValue() {
    return value;
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

  Type GetType() {
    return type;
  }
  
  long GetValue() {
    return value;
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
  string segment_str;
  
 public:
  CodeSegment(CodeElementVersion* r, CodeElementVersion* lhs) {
    result = r;
    left = lhs;
    oper = NULL;
    right = NULL;
  }
  
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

  CodeElementVersion* GetResult() {
    return result;
  }

  CodeElementVersion* GetLeft() {
    return left;
  }

  void SetLeft(CodeElementVersion* l) {
    left = l;
  }

   void SetRight(CodeElementVersion* r) {
    right = r;
  }

  CodeElementVersion* GetRight() {
    return right;
  }

  void SetOperator(CodeElement* o) {
    oper = o;
  }
  
  CodeElement* GetOperator() {
    return oper;
  }
  
  const string& GetKey() {
    if(key.size() == 0) {
      key = left->GetKey();
      if(oper && right) {
	key += oper->GetKey() + right->GetKey();
      }
    }
    
    return key;
  }
  
  const string& ToString() {
    // if(segment_str.size() == 0) {
      segment_str = result->GetKey() + '=' + left->GetKey();
      if(oper && right) {
	segment_str += oper->GetKey() + right->GetKey();
      }
      // }

    return segment_str;
  }
  
  bool IsUnary() {
    return !oper && !right;
  }
};

class CodeBlock {
  // cfg and original segments
  vector<CodeSegment*> original_segments;
  vector<CodeBlock*> parents;
  vector<CodeBlock*> children;
  // optimized segments
  vector<CodeSegment*> optimized_segments;
  multimap<const string, CodeSegment*> value_numbers;

  void Optimize(CodeSegment* s);
  
 public:
  CodeBlock() {
  }

  ~CodeBlock() {
    while(!original_segments.empty()) {
      CodeSegment* tmp = original_segments.front();
      original_segments.erase(original_segments.begin());
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
    original_segments.push_back(s);
  }

  void Optimize() {
    for(int i = 0; i < original_segments.size(); i++) {
      Optimize(original_segments[i]);
    }
  }
  
  void AddParent(CodeBlock* p) {
    parents.push_back(p);
  }

  void AddChild(CodeBlock* c) {
    children.push_back(c);
  }

  void Print() {
    // print current block
    for(int i = 0; i < original_segments.size(); i++) {
      cout << original_segments[i]->ToString() << endl;
    }
    // print childern
    cout << "---------" << endl;
    /*
    for(int i = 0; i < children.size(); i++) {
      children[i]->Print();
    }
    */
  }
  
  void PrintOptimized() {
    // print current block
    for(int i = 0; i < optimized_segments.size(); i++) {
      cout << optimized_segments[i]->ToString() << endl;
    }
    cout << "---------" << endl;
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


  CodeElement* MakeCodeElement(Type t) {
    return CodeElementFactory::Instance()->MakeCodeElement(t);
  }

  CodeElement* MakeCodeElement(Type t, long v) {
    return CodeElementFactory::Instance()->MakeCodeElement(t, v);
  }
  
  void Print() {
    root->Print();
  }

  void PrintOptimized() {
    root->PrintOptimized();
  }
  
  void LoadSegments();
  void Optimize() {
    root->Optimize();
  }
};

#endif
