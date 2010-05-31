#include "optimizer.h"

CodeElementFactory* CodeElementFactory::instance;
CodeElementFactory* CodeElementFactory::Instance() {
  if(!instance) {
    instance = new CodeElementFactory;
  }

  return instance;
}

void CodeBlock::Optimize(CodeSegment* s) {
  // remove invalidated expressions & propagation constants
  multimap<const string, CodeSegment*>::iterator iter;
  for(iter = value_numbers.begin(); iter != value_numbers.end(); iter++) {
    if(iter->second->GetLeft()->GetCodeElement() == s->GetResult()->GetCodeElement() ||
       (iter->second->GetRight() && 
	iter->second->GetRight()->GetCodeElement() == s->GetResult()->GetCodeElement())) {
      value_numbers.erase(iter->first);
    }
    // constant propagation 
    if(iter->second->GetResult()->GetCodeElement() == s->GetLeft()->GetCodeElement() &&
       iter->second->GetLeft()->GetType() == INT_LIT) {
      s->SetLeft(iter->second->GetLeft()->GetCodeElement()->GetVersion());
    }    
    if(s->GetRight() && iter->second->GetResult()->GetCodeElement() == s->GetRight()->GetCodeElement() &&
       iter->second->GetLeft()->GetType() == INT_LIT) {
      s->SetRight(iter->second->GetLeft()->GetCodeElement()->GetVersion());
    }
  }

  // TODO: apply other optimizations (const folding, identities, strength reduction)
  if(s->GetLeft()->GetType() == INT_LIT && s->GetRight() && s->GetRight()->GetType() == INT_LIT) {
    s->SetLeft(CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, s->GetLeft()->GetValue() + s->GetRight()->GetValue())->GetVersion());
    s->SetRight(NULL);
    s->SetOperator(NULL);
  }
  
  // associate common expressions
  multimap<const string, CodeSegment*>::iterator result = value_numbers.find(s->GetKey());
  if(result != value_numbers.end()) {
    // get last value
    multimap<const string, CodeSegment*>::iterator last = value_numbers.upper_bound(s->GetKey());
    --last;
    // perfer constant value
    CodeSegment* segment;
    if(last->second->IsUnary() && last->second->GetLeft()->GetType() == INT_LIT) {
      segment = new CodeSegment(s->GetResult()->GetCodeElement(), 
				last->second->GetLeft()->GetCodeElement());
    }
    else {
      segment = new CodeSegment(s->GetResult()->GetCodeElement(), 
				last->second->GetResult()->GetCodeElement());
    }
    
    optimized_segments.push_back(segment);
    // add to map    
    value_numbers.insert(pair<const string, CodeSegment*>(s->GetKey(), segment));
  }
  else {
    optimized_segments.push_back(s);
    value_numbers.insert(pair<const string, CodeSegment*>(s->GetKey(), s));
  }
}

void Optimizer::LoadSegments() {
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
  

  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_LIT, 25)));

  
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 0)));
 

  
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 0))); 
  
}
