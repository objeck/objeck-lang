#include "optimizer.h"

CodeElementFactory* CodeElementFactory::instance;
CodeElementFactory* CodeElementFactory::Instance() {
  if(!instance) {
    instance = new CodeElementFactory;
  }

  return instance;
}

void Optimizer::Optimize() {
  root = new CodeBlock();
  root->AddSegment(new CodeSegment(CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 0), 
				   CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 1), 
				   CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 0),
				   CodeElementFactory::Instance()->MakeCodeElement(ADD_OPER),
				   CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 0)));
  root->AddSegment(new CodeSegment(CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 0), 
				   CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 1)));
  root->AddSegment(new CodeSegment(CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 1), 
				   CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, 25)));
  root->AddSegment(new CodeSegment(CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 1), 
				   CodeElementFactory::Instance()->MakeCodeElement(INT_LIT, 74),
				   CodeElementFactory::Instance()->MakeCodeElement(ADD_OPER),
				   CodeElementFactory::Instance()->MakeCodeElement(INT_VAR, 0)));
}
