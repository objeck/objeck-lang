#include "optimizer.h"

CodeElementFactory* CodeElementFactory::instance;
CodeElementFactory* CodeElementFactory::Instance() {
  if(!instance) {
    instance = new CodeElementFactory;
  }

  return instance;
}

void CodeBlock::AddSegment(CodeSegment* s) {
  // TODO: search value numbers up
  if(s->IsUnary()) {
    multimap<const string, CodeSegment*>::iterator result = value_numbers.find(s->GetLeft()->GetKey());
    if(result != value_numbers.end()) {
      if(result->second->GetLeft()->GetType() == INT_LIT) {
	s->SetLeft(result->second->GetLeft());
      }
    }
  }


  // TODO: apply other optimizations (const folding, identities, strength reduction)
  

  // remove invalidated expressions
  multimap<const string, CodeSegment*>::iterator iter;
  for(iter = value_numbers.begin(); iter != value_numbers.end(); iter++) {
    if(iter->second->GetLeft()->GetCodeElement() == s->GetResult()->GetCodeElement() ||
       (iter->second->GetRight() && 
	iter->second->GetRight()->GetCodeElement() == s->GetResult()->GetCodeElement())) {
      value_numbers.erase(iter->first);
    }

    
  }
  
  // associate common expressions
  multimap<const string, CodeSegment*>::iterator result = value_numbers.find(s->GetKey());
  if(result != value_numbers.end()) {
    // get last value
    multimap<const string, CodeSegment*>::iterator last = value_numbers.upper_bound(s->GetKey());
    --last;
    // create new segment
    CodeSegment* segment = new CodeSegment(s->GetResult()->GetCodeElement(), 
					   last->second->GetResult()->GetCodeElement());
    optimized_segments.push_back(segment);
    // add to map    
    value_numbers.insert(pair<const string, CodeSegment*>(s->GetKey(), segment));
  }
  else {
    optimized_segments.push_back(s);
    value_numbers.insert(pair<const string, CodeSegment*>(s->GetKey(), s));
  }
  // add original
  original_segments.push_back(s);
}

void Optimizer::Optimize() {
  root = new CodeBlock();
  /*
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_VAR, 1)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_LIT, 25)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_LIT, 74),
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
  */

  /*
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 2), 
				   MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
  

  // root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
//				   MakeCodeElement(INT_LIT, 25)));

  
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 3), 
				   MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
  */

  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 0))); 
}
