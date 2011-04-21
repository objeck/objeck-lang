/***************************************************************************
 * Defines how the intermediate code is written to output files
 *
 * Copyright (c) 2008-2011, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "target.h"

using namespace backend;

/****************************
 * IntermediateFactory class
 ****************************/
IntermediateFactory* IntermediateFactory::instance;

IntermediateFactory* IntermediateFactory::Instance()
{
  if(!instance) {
    instance = new IntermediateFactory;
  }

  return instance;
}

InlineMethodData* IntermediateMethod::RegisterInlined(IntermediateMethod* called) {
  map<IntermediateMethod*, InlineMethodData*>::iterator found = registered_inlined_mthds.find(called);
  // not found
  InlineMethodData* data;
  if(found == registered_inlined_mthds.end()) {
    // TODO: delete items
    data = new InlineMethodData;
      
    data->locl_offset = space / sizeof(INT_VALUE);
    space += called->GetSpace() + sizeof(INT_VALUE);
    
    // add method data
    registered_inlined_mthds.insert(pair<IntermediateMethod*, InlineMethodData*>(called, data));
  }
  else {
    data = found->second;;
  }
  
  InlineClassInstanceData* cls_inst_data;
  if(klass->GetId() != called->GetClass()->GetId()) {
    map<IntermediateClass*, InlineClassInstanceData*>::iterator cls_found = registered_inlined_clss.find(called->GetClass());
    // not found
    if(cls_found == registered_inlined_clss.end()) {
      cls_inst_data = new InlineClassInstanceData;
      // instance offset
      int inst_offset = klass->GetInstanceSpace() / sizeof(INT_VALUE);
      klass->SetInstanceSpace(klass->GetInstanceSpace() + called->GetClass()->GetInstanceSpace());
      cls_inst_data->inst_offset = inst_offset;
      // class offset
      int cls_offset = klass->GetClassSpace() / sizeof(INT_VALUE);
      klass->SetClassSpace(klass->GetClassSpace() + called->GetClass()->GetClassSpace());
      cls_inst_data->cls_offset = cls_offset;      
      // add instance & class data
      registered_inlined_clss.insert(pair<IntermediateClass*, InlineClassInstanceData*>(called->GetClass(), cls_inst_data));
    }
    else {
      cls_inst_data = cls_found->second;
    }
  }
  else {
    cls_inst_data = new InlineClassInstanceData;
    cls_inst_data->inst_offset = 0;
    cls_inst_data->cls_offset = 0;
  }
  data->cls_inst_data = cls_inst_data;
  
  return data;
}

/****************************
 * Writes target code to an
 * output file.
 ****************************/
void TargetEmitter::Emit()
{
#ifdef _DEBUG
  cout << "\n--------- Emitting Target Code ---------" << endl;
  program->Debug();
#endif

  if(is_lib) {
    // TODO: better error handling
    if(file_name.rfind(".obl") == string::npos) {
      cerr << "Error: Libraries must end in '.obl'" << endl;
      exit(1);
    }
  } else {
    // TODO: better error handling
    if(file_name.rfind(".obe") == string::npos) {
      cerr << "Error: Executables must end in '.obe'" << endl;
      exit(1);
    }
  }
  
  ofstream* file_out = new ofstream(file_name.c_str(), ofstream::binary);
  if(file_out && file_out->is_open()) {
    program->Write(file_out, is_lib);
    file_out->close();
    cout << "Wrote target file: '" << file_name << "'" << endl;
  }
  else {
    cerr << "Unable to write file: '" << file_name << "'" << endl;
  }
  
  delete file_out;
  file_out = NULL;
}
