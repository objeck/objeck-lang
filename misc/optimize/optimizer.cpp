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
/* NOT NEEDED SINCE SSA
    if(iter->second->GetLeft()->GetKey() == s->GetResult()->GetKey() ||
       (iter->second->GetRight() && 
	iter->second->GetRight()->GetCodeElement() == s->GetResult()->GetCodeElement())) {
      value_numbers.erase(iter->first);
    }
*/
    // constant propagation 
    if(iter->second->GetResult()->GetKey() == s->GetLeft()->GetKey() &&
       iter->second->GetLeft()->GetType() == INT_LIT) {
      s->SetLeft(iter->second->GetLeft()->GetCodeElement()->GetVersion());
    }    
    if(s->GetRight() && iter->second->GetResult()->GetKey() == s->GetRight()->GetKey() &&
       iter->second->GetLeft()->GetType() == INT_LIT) {
      s->SetRight(iter->second->GetLeft()->GetCodeElement()->GetVersion());
    }
  }
  
  // TODO: apply other optimizations (const folding, identities, strength reduction)
  if(s->GetLeft()->GetType() == INT_LIT && s->GetRight() && s->GetRight()->GetType() == INT_LIT) {
    CodeElementVersion* oper_result;
    switch(s->GetOperator()->GetType()) {
    case ADD_OPER:
      oper_result = CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, s->GetLeft()->GetValue() + 
								    s->GetRight()->GetValue())->GetVersion();
      break;

    case MUL_OPER:
      oper_result = CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, s->GetLeft()->GetValue() * 
								    s->GetRight()->GetValue())->GetVersion();
      break;
    }
    s->SetLeft(oper_result);
    s->SetRight(NULL);
    s->SetOperator(NULL);
  }
 
  // associate common expressions
  multimap<const string, CodeSegment*>::iterator result = value_numbers.find(s->GetKey());
  if(result != value_numbers.end()) {
    // perfer constant value
    CodeSegment* segment;
    if(result->second->IsUnary() && result->second->GetLeft()->GetType() == INT_LIT) {
      segment = new CodeSegment(s->GetResult(), result->second->GetLeft());
    }
    else {
      segment = new CodeSegment(s->GetResult(), result->second->GetResult());
    }
    optimized_segments.push_back(segment);
    // add to map    
    value_numbers.insert(pair<const string, CodeSegment*>(segment->GetKey(), segment));
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
				   MakeCodeElement(INT_LIT, 0)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_LIT, 1)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 2), 
				   MakeCodeElement(INT_LIT, 2)));

  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 20), 
				   MakeCodeElement(INT_LIT, 20)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 21), 
				   MakeCodeElement(INT_LIT, 21)));
  

  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_VAR, 20), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 21)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 1), 
				   MakeCodeElement(INT_VAR, 20), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 21)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
				   MakeCodeElement(INT_LIT, 17)));
  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 2), 
				   MakeCodeElement(INT_VAR, 20), 
				   MakeCodeElement(ADD_OPER),
				   MakeCodeElement(INT_VAR, 21)));
  */

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
  

  root->AddSegment(new CodeSegment(MakeCodeElement(INT_VAR, 0), 
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
