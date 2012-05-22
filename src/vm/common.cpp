/***************************************************************************
 * VM common.
 *
 * Copyright (c) 2008-2012, Randy Hollines
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
 * - Neither the name of the Objeck Team nor the names of its
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

#include "common.h"
#include "loader.h"
#include "interpreter.h"

#ifdef _WIN32
list<HANDLE> StackProgram::thread_ids;
CRITICAL_SECTION StackProgram::program_cs;
CRITICAL_SECTION StackMethod::virutal_cs;
#else
list<pthread_t> StackProgram::thread_ids;
pthread_mutex_t StackProgram::program_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t StackProgram::condition_var = PTHREAD_COND_INITIALIZER;
pthread_mutex_t StackMethod::virtual_mutex = PTHREAD_MUTEX_INITIALIZER;

#endif
unordered_map<string, StackMethod*> StackMethod::virutal_cache;

/********************************
 * ObjectSerializer struct
 ********************************/
void ObjectSerializer::CheckObject(long* mem, bool is_obj, long depth) {
  if(mem) {
    SerializeByte(1);
    StackClass* cls = MemoryManager::GetClass(mem);
    if(cls) {
      // write id
      SerializeInt(cls->GetId());
      
      if(!WasSerialized(mem)) {
#ifdef _DEBUG
	long mem_size = cls->GetInstanceMemorySize();
	
#ifdef _X64
	mem_size *= 2;
#endif
	for(int i = 0; i < depth; i++) {
	  cout << "\t";
	}
	cout << "\t----- SERIALIZING object: cls_id=" << cls->GetId() << ", mem_id=" 
	     << cur_id << ", size=" << mem_size << " byte(s) -----" << endl;
#endif
	CheckMemory(mem, cls->GetDeclarations(), cls->GetNumberDeclarations(), depth + 1);
      } 
    }
    else {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        cout << "\t";
      }
      cout <<"$: addr/value=" << mem << endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(!WasSerialized(mem)) {
	long* array = (mem);
	const long size = array[0];
	const long dim = array[1];
	long* objects = (long*)(array + 2 + dim);

#ifdef _DEBUG
	for(int i = 0; i < depth; i++) {
	  cout << "\t";
	}
	cout << "\t----- SERIALIZE: size=" << (size * sizeof(INT_VALUE)) << " -----" << endl;	
#endif
	
	for(long k = 0; k < size; k++) {
	  CheckObject((long*)objects[k], false, 2);
	}
      }
    }
  }
  else {
    SerializeByte(0);
  }
}

void ObjectSerializer::CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, long depth) {
  // check method
  for(long i = 0; i < dcls_size; i++) {
#ifdef _DEBUG
    for(long j = 0; j < depth; j++) {
      cout << "\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
    case INT_PARM: {
#ifdef _DEBUG
      cout << "\t" << i << ": ----- serializing int: value=" << (*mem) << ", size=" << sizeof(INT_VALUE) << " byte(s) -----" << endl;
#endif
      SerializeInt(*mem);
      // update
      mem++;
    }
      break;

    case FLOAT_PARM: {
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
#ifdef _DEBUG
      cout << "\t" << i << ": ----- serializing float: value=" << value << ", size=" 
	   << sizeof(FLOAT_VALUE) << " byte(s) -----" << endl;
#endif
      SerializeFloat(value);
      // update
      mem += 2;
    }
    break;
    
    case BYTE_ARY_PARM: {
      long* array = (long*)(*mem);
      if(array) {
	SerializeByte(1);
	// mark data
	if(!WasSerialized((long*)(*mem))) {
	  const long array_size = array[0];
#ifdef _DEBUG
	  cout << "\t" << i << ": ----- serializing byte array: mem_id=" << cur_id << ", size=" 
	       << array_size << " byte(s) -----" << endl;
#endif
	  // write metadata
	  SerializeInt(array[0]);
	  SerializeInt(array[1]);
	  SerializeInt(array[2]);
	  BYTE_VALUE* array_ptr = (BYTE_VALUE*)(array + 3);
	  // values
	  SerializeBytes(array_ptr, array_size);
	}
      }
      else {
	SerializeByte(0);
      }
      // update
      mem++;
    }
      break;
      
    case INT_ARY_PARM: {
      long* array = (long*)(*mem);
      if(array) {
	SerializeByte(1);
	// mark data
	if(!WasSerialized((long*)(*mem))) {
	  const long array_size = array[0];
#ifdef _DEBUG
	  cout << "\t" << i << ": ----- serializing int array: mem_id=" << cur_id << ", size=" 
	       << array_size << " byte(s) -----" << endl;
#endif
	  // write metadata
	  SerializeInt(array[0]);
	  SerializeInt(array[1]);
	  SerializeInt(array[2]);
	  long* array_ptr = array + 3;	
	  // values
	  for(int i = 0; i < array_size; i++) {
	    SerializeInt(array_ptr[i]);
	  }
	}
      }
      else {
	SerializeByte(0);
      }
      // update
      mem++;
    }
      break;
      
    case FLOAT_ARY_PARM: {
      long* array = (long*)(*mem);
      if(array) {
	SerializeByte(1);
	// mark data
	if(!WasSerialized((long*)(*mem))) {
	  const long array_size = array[0];
#ifdef _DEBUG
	  cout << "\t" << i << ": ----- serializing float array: mem_id=" << cur_id << ", size=" 
	       << array_size << " byte(s) -----" << endl;
#endif
	  // write metadata
	  SerializeInt(array[0]);
	  SerializeInt(array[1]);
	  SerializeInt(array[2]);
	  FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 3);
	  // write values
	  SerializeBytes(array_ptr, array_size * sizeof(FLOAT_VALUE));
	}
      }
      else {
	SerializeByte(0);
      }
      // update
      mem++;
    }
      break;
      
    case OBJ_PARM: {
      // check object
      CheckObject((long*)(*mem), true, depth + 1);
      // update
      mem++;
    }
      break;
      
    default:
      break;
    }
  }
}

void ObjectSerializer::Serialize(long* inst) {
  next_id = 0;
  CheckObject(inst, true, 0);
}

/********************************
 * ObjectDeserializer class
 ********************************/
long* ObjectDeserializer::DeserializeObject() {
  INT_VALUE obj_id = DeserializeInt();
  cls = Loader::GetProgram()->GetClass(obj_id);
  if(cls) {
    INT_VALUE mem_id = DeserializeInt();
    if(mem_id < 0) {
      instance = MemoryManager::AllocateObject(cls->GetId(), (long*)op_stack, *stack_pos);
      mem_cache[-mem_id] = instance;
    }
    else {
      map<INT_VALUE, long*>::iterator found = mem_cache.find(mem_id);
      if(found == mem_cache.end()) {
	return NULL;
      }      
      return found->second;
    }
  }
  else {
    return NULL;
  }

  long dclr_pos = 0;  
  StackDclr** dclrs = cls->GetDeclarations();
  const long dclr_num = cls->GetNumberDeclarations();
  while(dclr_pos < dclr_num && buffer_offset < buffer_array_size) {
    ParamType type = dclrs[dclr_pos++]->type;
    
    switch(type) {
    case INT_PARM:
      instance[instance_pos++] = DeserializeInt();
#ifdef _DEBUG
      cout << "--- deserialization: int value=" << instance[instance_pos - 1] << " ---" << endl;
#endif
      break;

    case FLOAT_PARM: {
      FLOAT_VALUE value = DeserializeFloat();
      memcpy(&instance[instance_pos], &value, sizeof(value));
#ifdef _DEBUG
      cout << "--- deserialization: float value=" << value << " ---" << endl;
#endif
      instance_pos += 2;
    }
      break;
      
    case BYTE_ARY_PARM: {
      if(!DeserializeByte()) {
	instance[instance_pos++] = 0;
      }
      else {
	INT_VALUE mem_id = DeserializeInt();
	if(mem_id < 0) {
	  const long byte_array_size = DeserializeInt();
	  const long byte_array_dim = DeserializeInt();
	  const long byte_array_size_dim = DeserializeInt();
	  long* byte_array = (long*)MemoryManager::AllocateArray(byte_array_size +
								 ((byte_array_dim + 2) *
								  sizeof(long)),
								 BYTE_ARY_TYPE,
								 op_stack, *stack_pos);
	  BYTE_VALUE* byte_array_ptr = (BYTE_VALUE*)(byte_array + 3);
	  byte_array[0] = byte_array_size;
	  byte_array[1] = byte_array_dim;
	  byte_array[2] = byte_array_size_dim;	
	  // copy content
	  memcpy(byte_array_ptr, buffer + buffer_offset, byte_array_size);
	  buffer_offset += byte_array_size;
#ifdef _DEBUG
	  cout << "--- deserialization: byte array; value=" << byte_array <<  ", size=" << byte_array_size << " ---" << endl;
#endif
	  // update cache
	  mem_cache[-mem_id] = byte_array;
	  instance[instance_pos++] = (long)byte_array;
	}
	else {
	  map<INT_VALUE, long*>::iterator found = mem_cache.find(mem_id);
	  if(found == mem_cache.end()) {
	    return NULL;
	  } 
	  instance[instance_pos++] = (long)found->second;
	}
      }
    }
      break;
      
    case INT_ARY_PARM: {
      if(!DeserializeByte()) {
	instance[instance_pos++] = 0;
      }
      else {
	INT_VALUE mem_id = DeserializeInt();
	if(mem_id < 0) {
	  const long array_size = DeserializeInt();
	  const long array_dim = DeserializeInt();
	  const long array_size_dim = DeserializeInt();	
	  long* array = (long*)MemoryManager::AllocateArray(array_size + array_dim + 2, 
							    INT_TYPE, op_stack, *stack_pos);
	  array[0] = array_size;
	  array[1] = array_dim;
	  array[2] = array_size_dim;
	  long* array_ptr = array + 3;	
	  // copy content
	  for(int i = 0; i < array_size; i++) {
	    array_ptr[i] = DeserializeInt();
	  }
#ifdef _DEBUG
	  cout << "--- deserialization: int array; value=" << array <<  ",  size=" << array_size << " ---" << endl;
#endif
	  // update cache
	  mem_cache[-mem_id] = array;
	  instance[instance_pos++] = (long)array;
	}
	else {
	  map<INT_VALUE, long*>::iterator found = mem_cache.find(mem_id);
	  if(found == mem_cache.end()) {
	    return NULL;
	  } 
	  instance[instance_pos++] = (long)found->second;
	}
      }
    }
      break;
      
    case FLOAT_ARY_PARM: {
      if(!DeserializeByte()) {
	instance[instance_pos++] = 0;
      }
      else {
	INT_VALUE mem_id = DeserializeInt();
	if(mem_id < 0) {
	  const long array_size = DeserializeInt();
	  const long array_dim = DeserializeInt();
	  const long array_size_dim = DeserializeInt();
	  long* array = (long*)MemoryManager::AllocateArray(array_size * 2 + array_dim + 2, 
							    INT_TYPE, op_stack, *stack_pos);
	
	  array[0] = array_size;
	  array[1] = array_dim;
	  array[2] = array_size_dim;
	  FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 3);	
	  // copy content
	  memcpy(array_ptr, buffer + buffer_offset, array_size * sizeof(FLOAT_VALUE));
	  buffer_offset += array_size * sizeof(FLOAT_VALUE);
#ifdef _DEBUG
	  cout << "--- deserialization: float array; value=" << array <<  ", size=" << array_size << " ---" << endl;
#endif
	  // update cache
	  mem_cache[-mem_id] = array;
	  instance[instance_pos++] = (long)array;
	}
	else {
	  map<INT_VALUE, long*>::iterator found = mem_cache.find(mem_id);
	  if(found == mem_cache.end()) {
	    return NULL;
	  } 
	  instance[instance_pos++] = (long)found->second;
	}
      }
    }
      break;
      
    case OBJ_PARM: {
      if(!DeserializeByte()) {
	instance[instance_pos++] = 0;
      }
      else {
	ObjectDeserializer deserializer(buffer, buffer_offset, mem_cache, buffer_array_size, op_stack, stack_pos);
	instance[instance_pos++] = (long)deserializer.DeserializeObject();
	// TODO: refactor this to be faster
	buffer_offset = deserializer.GetOffset();
	mem_cache = deserializer.GetMemoryCache();
      }
    }
      break;
      
    default:
      break;
    }
  }
  
  return instance;
}

/********************************
 * SDK functions
 ********************************/
#ifndef _UTILS
void APITools_MethodCall(long* op_stack, long *stack_pos, long *instance, 
			 int cls_id, int mthd_id) {
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      cerr << ">>> DLL call: Unable to locate method; id=" << mthd_id << " <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << ">>> DLL call: Unable to locate class; id=" << cls_id << " <<<" << endl;
    exit(1);
  }
}

void APITools_MethodCall(long* op_stack, long *stack_pos, long *instance, 
			 const char* cls_id, const char* mthd_id) 
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      cerr << ">>> DLL call: Unable to locate method; name=': " << mthd_id << "' <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << ">>> DLL call: Unable to locate class; name='" << cls_id << "' <<<" << endl;
    exit(1);
  }
}

void APITools_MethodCallId(long* op_stack, long *stack_pos, long *instance, 
			 const int cls_id, const int mthd_id) 
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      cerr << ">>> DLL call: Unable to locate method; id=: " << mthd_id << " <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << ">>> DLL call: Unable to locate class; id=" << cls_id << " <<<" << endl;
    exit(1);
  }
}

#endif
