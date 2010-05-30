#include "optimizer.h"

void Optimizer::Optimize() {
  root = new CodeBlock();
  root->AddSegment(new CodeSegment(new CodeElement(INT_VAR, 0), new CodeElement(INT_LIT, 13)));
  root->AddSegment(new CodeSegment(new CodeElement(INT_VAR, 1), 
				   new CodeElement(INT_VAR, 0),
				   new CodeElement(ADD_OPER),
				   new CodeElement(INT_VAR, 0)));
  root->AddSegment(new CodeSegment(new CodeElement(INT_VAR, 0), new CodeElement(INT_VAR, 1)));
}
