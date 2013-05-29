/***************************************************************************
 * VM common.
 *
 * Copyright (c) 2008-2013, Randy Hollines
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
 * - Neither the name of the Objeck team nor the names of its
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
#ifndef _UTILS
#include "os/windows/windows.h"
#endif
#include "os/windows/memory.h"
#else
#include "os/posix/posix.h"
#include "os/posix/memory.h"
#endif

#ifdef _WIN32
list<HANDLE> StackProgram::thread_ids;
CRITICAL_SECTION StackProgram::program_cs;
CRITICAL_SECTION StackMethod::virutal_cs;
CRITICAL_SECTION StackProgram::prop_cs;
#else
list<pthread_t> StackProgram::thread_ids;
pthread_mutex_t StackProgram::program_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackMethod::virtual_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackProgram::prop_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
unordered_map<wstring, StackMethod*> StackMethod::virutal_cache;
map<wstring, wstring> StackProgram::properties_map;

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
          wcout << L"\t";
        }
        wcout << L"\t----- SERIALIZING object: cls_id=" << cls->GetId() << L", mem_id=" 
							<< cur_id << L", size=" << mem_size << L" byte(s) -----" << endl;
#endif
        CheckMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), depth + 1);
      } 
    }
    else {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        wcout << L"\t";
      }
      wcout <<"$: addr/value=" << mem << endl;
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
          wcout << L"\t";
        }
        wcout << L"\t----- SERIALIZE: size=" << (size * sizeof(INT_VALUE)) << L" -----" << endl;	
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
      wcout << L"\t";
    }
#endif
    
    // update address based upon type
    switch(dclrs[i]->type) {
    case CHAR_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": ----- serializing char: value=" 
						<< (*mem) << L", size=" << sizeof(INT_VALUE) << L" byte(s) -----" << endl;
#endif
      SerializeChar((wchar_t)*mem);
      // update
      mem++;
      break;
      
    case INT_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": ----- serializing int: value=" 
						<< (*mem) << L", size=" << sizeof(INT_VALUE) << L" byte(s) -----" << endl;
#endif
      SerializeInt(*mem);
      // update
      mem++;
      break;

    case FLOAT_PARM: {
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
#ifdef _DEBUG
      wcout << L"\t" << i << L": ----- serializing float: value=" << value << L", size=" 
						<< sizeof(FLOAT_VALUE) << L" byte(s) -----" << endl;
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
          wcout << L"\t" << i << L": ----- serializing byte array: mem_id=" << cur_id << L", size=" 
								<< array_size << L" byte(s) -----" << endl;
#endif
          // write metadata
          SerializeInt(array[0]);
          SerializeInt(array[1]);
          SerializeInt(array[2]);
          char* array_ptr = (char*)(array + 3);
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

    case CHAR_ARY_PARM: {
      long* array = (long*)(*mem);
      if(array) {
        SerializeByte(1);
        // mark data
        if(!WasSerialized((long*)(*mem))) {
					// convert
					const string buffer = UnicodeToBytes((wchar_t*)(array + 3));
					const long array_size = buffer.size();	  
#ifdef _DEBUG
          wcout << L"\t" << i << L": ----- serializing char array: mem_id=" << cur_id << L", size=" 
								<< array_size << L" byte(s) -----" << endl;
#endif	  
          // write metadata
          SerializeInt(array_size);
          SerializeInt(array[1]);
          SerializeInt(array_size);
          
          // values
          SerializeBytes(buffer.c_str(), array_size);
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
          wcout << L"\t" << i << L": ----- serializing int array: mem_id=" << cur_id << L", size=" 
								<< array_size << L" byte(s) -----" << endl;
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
          wcout << L"\t" << i << L": ----- serializing float array: mem_id=" << cur_id << L", size=" 
								<< array_size << L" byte(s) -----" << endl;
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
      instance = MemoryManager::AllocateObject(cls->GetId(), (long*)op_stack, *stack_pos, false);
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
  StackDclr** dclrs = cls->GetInstanceDeclarations();
  const long dclr_num = cls->GetNumberInstanceDeclarations();
  while(dclr_pos < dclr_num && buffer_offset < buffer_array_size) {
    ParamType type = dclrs[dclr_pos++]->type;

    switch(type) {
    case CHAR_PARM:
      instance[instance_pos++] = DeserializeChar();
#ifdef _DEBUG
      wcout << L"--- deserialization: char value=" << instance[instance_pos - 1] << L" ---" << endl;
#endif
      break;
      
    case INT_PARM:
      instance[instance_pos++] = DeserializeInt();
#ifdef _DEBUG
      wcout << L"--- deserialization: int value=" << instance[instance_pos - 1] << L" ---" << endl;
#endif
      break;

    case FLOAT_PARM: {
      FLOAT_VALUE value = DeserializeFloat();
      memcpy(&instance[instance_pos], &value, sizeof(value));
#ifdef _DEBUG
      wcout << L"--- deserialization: float value=" << value << L" ---" << endl;
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
																																	sizeof(long)), BYTE_ARY_TYPE,
																																 op_stack, *stack_pos, false);
          char* byte_array_ptr = (char*)(byte_array + 3);
          byte_array[0] = byte_array_size;
          byte_array[1] = byte_array_dim;
          byte_array[2] = byte_array_size_dim;	
          // copy content
          memcpy(byte_array_ptr, buffer + buffer_offset, byte_array_size);
          buffer_offset += byte_array_size;
#ifdef _DEBUG
          wcout << L"--- deserialization: byte array; value=" << byte_array <<  ", size=" << byte_array_size << L" ---" << endl;
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
      
    case CHAR_ARY_PARM: {
      if(!DeserializeByte()) {
        instance[instance_pos++] = 0;
      }
      else {
				INT_VALUE mem_id = DeserializeInt();
        if(mem_id < 0) {
          long char_array_size = DeserializeInt();
          const long char_array_dim = DeserializeInt();
					long char_array_size_dim = DeserializeInt();

          // copy content
					char* in = new char[char_array_size + 1];
          memcpy(in, buffer + buffer_offset, char_array_size);
          buffer_offset += char_array_size;
					in[char_array_size] = '\0';
	  
					const wstring out = BytesToUnicode(in);

					// clean up
					delete[] in;
					in = NULL;
	  
#ifdef _DEBUG
          wcout << L"--- deserialization: char array; value=" << out <<  ", size=" << char_array_size << L" ---" << endl;
#endif
					char_array_size = char_array_size_dim = out.size();
					long* char_array = (long*)MemoryManager::AllocateArray(char_array_size +
																																 ((char_array_dim + 2) *
																																	sizeof(long)), CHAR_ARY_TYPE,
																																 op_stack, *stack_pos, false);
          char_array[0] = char_array_size;
          char_array[1] = char_array_dim;
          char_array[2] = char_array_size_dim;
	  
					wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
					memcpy(char_array_ptr, out.c_str(), char_array_size * sizeof(wchar_t));
	  
          // update cache
          mem_cache[-mem_id] = char_array;
          instance[instance_pos++] = (long)char_array;
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
          long* array = (long*)MemoryManager::AllocateArray(array_size + array_dim + 2, INT_TYPE, 
																														op_stack, *stack_pos, false);
          array[0] = array_size;
          array[1] = array_dim;
          array[2] = array_size_dim;
          long* array_ptr = array + 3;	
          // copy content
          for(int i = 0; i < array_size; i++) {
            array_ptr[i] = DeserializeInt();
          }
#ifdef _DEBUG
          wcout << L"--- deserialization: int array; value=" << array <<  ",  size=" << array_size << L" ---" << endl;
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
          long* array = (long*)MemoryManager::AllocateArray(array_size * 2 + array_dim + 2, INT_TYPE, 
																														op_stack, *stack_pos, false);

          array[0] = array_size;
          array[1] = array_dim;
          array[2] = array_size_dim;
          FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 3);	
          // copy content
          memcpy(array_ptr, buffer + buffer_offset, array_size * sizeof(FLOAT_VALUE));
          buffer_offset += array_size * sizeof(FLOAT_VALUE);
#ifdef _DEBUG
          wcout << L"--- deserialization: float array; value=" << array <<  ", size=" << array_size << L" ---" << endl;
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
void APITools_MethodCall(long* op_stack, long *stack_pos, long *instance, int cls_id, int mthd_id) {
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      cerr << L">>> DLL call: Unable to locate method; id=" << mthd_id << L" <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << endl;
    exit(1);
  }
}

void APITools_MethodCall(long* op_stack, long *stack_pos, long *instance, 
												 const wchar_t* cls_id, const wchar_t* mthd_id) 
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, instance, false);
    }
    else {
      wcerr << L">>> Unable to locate method; name=': " << mthd_id << L"' <<<" << endl;
      exit(1);
    }
  }
  else {
    wcerr << L">>> Unable to locate class; name='" << cls_id << L"' <<<" << endl;
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
      cerr << L">>> DLL call: Unable to locate method; id=: " << mthd_id << L" <<<" << endl;
      exit(1);
    }
  }
  else {
    cerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << endl;
    exit(1);
  }
}

/********************************
 *  TrapManager class
 ********************************/
void TrapProcessor::CreateNewObject(const wstring &cls_id, long* &op_stack, long* &stack_pos) {
  long* obj = MemoryManager::AllocateObject(cls_id.c_str(), (long*)op_stack, *stack_pos, false);
  if(obj) {
    // instance will be put on stack by method call
    const wstring mthd_name = cls_id + L":New:";
    APITools_MethodCall((long*)op_stack, stack_pos, obj, cls_id.c_str(), mthd_name.c_str());
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

/********************************
 * Creates a container for a method
 ********************************/
long* TrapProcessor::CreateMethodObject(long* cls_obj, StackMethod* mthd, StackProgram* program, 
																				long* &op_stack, long* &stack_pos) {
  long* mthd_obj = MemoryManager::AllocateObject(program->GetMethodObjectId(),
																								 (long*)op_stack, *stack_pos,
																								 false);
  // method and class object
  mthd_obj[0] = (long)mthd;
  mthd_obj[1] = (long)cls_obj;
      
  // set method name
  const wstring &qual_mthd_name = mthd->GetName();
  const size_t semi_qual_mthd_index = qual_mthd_name.find(':');
  if(semi_qual_mthd_index == wstring::npos) {
    wcerr << L">>> Internal error: invalid method name <<<" << endl;
    exit(1);
  }
      
  const wstring &semi_qual_mthd_string = qual_mthd_name.substr(semi_qual_mthd_index + 1);
  const size_t mthd_index = semi_qual_mthd_string.find(':');
  if(mthd_index == wstring::npos) {
    wcerr << L">>> Internal error: invalid method name <<<" << endl;
    exit(1);
  }
  const wstring &mthd_string = semi_qual_mthd_string.substr(0, mthd_index);
  mthd_obj[2] = (long)CreateStringObject(mthd_string, program, op_stack, stack_pos);

  // parse parameter wstring      
  int index = 0;
  const wstring &params_string = semi_qual_mthd_string.substr(mthd_index + 1);
  vector<long*> data_type_obj_holder;
  while(index < (int)params_string.size()) {
    long* data_type_obj = MemoryManager::AllocateObject(program->GetDataTypeObjectId(),
																												(long*)op_stack, *stack_pos,
																												false);
    data_type_obj_holder.push_back(data_type_obj);
        
    switch(params_string[index]) {
    case L'l':
      data_type_obj[0] = -1000;
      index++;
      break;
	  
    case L'b':
      data_type_obj[0] = -999;
      index++;
      break;

    case L'i':
      data_type_obj[0] = -997;
      index++;
      break;

    case L'f':
      data_type_obj[0] = -996;
      index++;
      break;

    case L'c':
      data_type_obj[0] = -998;
      index++;
      break;
	  
    case L'o': {
      data_type_obj[0] = -995;
      index++;
      const int start_index = index + 1;
      while(index < (int)params_string.size() && params_string[index] != L',') {
				index++;
      }
      data_type_obj[1] = (long)CreateStringObject(params_string.substr(start_index, index - 2), 
																									program, op_stack, stack_pos);
    }
      break;
	  
    case L'm':
      data_type_obj[0] = -994;
      index++;
      while(index < (int)params_string.size() && params_string[index] != L'~') {
				index++;
      }
      while(index < (int)params_string.size() && params_string[index] != L',') {
				index++;
      }
      break;
	  
    default:
#ifdef _DEBUG
      assert(false);
#endif
      break;
    }
	
    // check array dimension
    int dimension = 0;
    while(index < (int)params_string.size() && params_string[index] == L'*') {
      dimension++;
      index++;
    }
    data_type_obj[2] = dimension;
	
    // match ','
    index++;
  }
      
  // create type array
  const long type_obj_array_size = (long)data_type_obj_holder.size();
  const long type_obj_array_dim = 1;
  long* type_obj_array = (long*)MemoryManager::AllocateArray(type_obj_array_size +
																														 type_obj_array_dim + 2,
																														 INT_TYPE, op_stack,
																														 *stack_pos, false);
  type_obj_array[0] = type_obj_array_size;
  type_obj_array[1] = type_obj_array_dim;
  type_obj_array[2] = type_obj_array_size;
  long* type_obj_array_ptr = type_obj_array + 3;
  // copy types objects
  for(int i = 0; i < type_obj_array_size; i++) {
    type_obj_array_ptr[i] = (long)data_type_obj_holder[i];
  }
  // set type array
  mthd_obj[3] = (long)type_obj_array;
      
  return mthd_obj;
}
    
/********************************
 * Creates a container for a class
 ********************************/
void TrapProcessor::CreateClassObject(StackClass* cls, long* cls_obj, long* &op_stack, 
																			long* &stack_pos, StackProgram* program) {
  // create and set methods
  const long mthd_obj_array_size = cls->GetMethodCount();
  const long mthd_obj_array_dim = 1;
  long* mthd_obj_array = (long*)MemoryManager::AllocateArray(mthd_obj_array_size +
																														 mthd_obj_array_dim + 2,
																														 INT_TYPE, op_stack,
																														 *stack_pos, false);
      
  mthd_obj_array[0] = mthd_obj_array_size;
  mthd_obj_array[1] = mthd_obj_array_dim;
  mthd_obj_array[2] = mthd_obj_array_size;
      
  StackMethod** methods = cls->GetMethods();
  long* mthd_obj_array_ptr = mthd_obj_array + 3;
  for(int i = 0; i < mthd_obj_array_size; i++) {
    long* mthd_obj = CreateMethodObject(cls_obj, methods[i], program, op_stack, stack_pos);
    mthd_obj_array_ptr[i] = (long)mthd_obj;
  }
  cls_obj[1] = (long)mthd_obj_array;
}

/********************************
 * Create a string instance
 ********************************/
long* TrapProcessor::CreateStringObject(const wstring &value_str, StackProgram* program, 
																				long* &op_stack, long* &stack_pos) {
  // create character array
  const long char_array_size = value_str.size();
  const long char_array_dim = 1;
  long* char_array = (long*)MemoryManager::AllocateArray(char_array_size + 1 +
																												 ((char_array_dim + 2) *
																													sizeof(long)),
																												 CHAR_ARY_TYPE,
																												 op_stack, *stack_pos,
																												 false);
  char_array[0] = char_array_size + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy wstring
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
  wcsncpy(char_array_ptr, value_str.c_str(), char_array_size);
      
  // create 'System.String' object instance
  long* str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(),
																								(long*)op_stack, *stack_pos,
																								false);
  str_obj[0] = (long)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;
      
  return str_obj;
}

/********************************
 * Creates a Date object with
 * current time
 ********************************/
void TrapProcessor::ProcessCurrentTime(StackFrame* frame, bool is_gmt) 
{
  time_t raw_time;
  raw_time = time(NULL);

  struct tm* curr_time;
  if(is_gmt) {
    curr_time = gmtime(&raw_time);
  }
  else {
    curr_time = localtime(&raw_time);
  }

  long* instance = (long*)frame->mem[0];
  if(instance) {
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Date/time calculations
 ********************************/
void TrapProcessor::ProcessAddTime(TimeInterval t, long* &op_stack, long* &stack_pos)
{  
  long value = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(instance) {
    // calculate change in seconds
    long offset;
    switch(t) {
    case DAY_TIME:
      offset = 86400 * value;
      break;

    case HOUR_TIME:
      offset = 3600 * value;
      break;

    case MIN_TIME:
      offset = 60 * value;
      break;

    default:
      offset = value;
      break;
    }

    // create time structure
    struct tm set_time;
    set_time.tm_mday = instance[0];          // day
    set_time.tm_mon = instance[1] - 1;       // month
    set_time.tm_year = instance[2] - 1900;   // year
    set_time.tm_hour = instance[3];          // hours
    set_time.tm_min = instance[4];           // mins
    set_time.tm_sec = instance[5];           // secs
    set_time.tm_isdst = instance[6] > 0;     // savings time

    // calculate difference
    time_t raw_time = mktime(&set_time);
    raw_time += offset;  

    struct tm* curr_time;
    if(instance[8]) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
  }
}

/********************************
 * Creates a Date object with 
 * specified time
 ********************************/
void TrapProcessor::ProcessSetTime1(long* &op_stack, long* &stack_pos) 
{
  // get time values
  long is_gmt = PopInt(op_stack, stack_pos);
  long year = PopInt(op_stack, stack_pos);
  long month = PopInt(op_stack, stack_pos);
  long day = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(instance) {
    // get current time
    time_t raw_time;
    time(&raw_time);  
    struct tm* curr_time;
    if(is_gmt) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // update time
    curr_time->tm_year = year - 1900;
    curr_time->tm_mon = month - 1;
    curr_time->tm_mday = day;
    mktime(curr_time);

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Sets a time instance
 ********************************/
void TrapProcessor::ProcessSetTime2(long* &op_stack, long* &stack_pos)
{
  // get time values
  long is_gmt = PopInt(op_stack, stack_pos);
  long secs = PopInt(op_stack, stack_pos);
  long mins = PopInt(op_stack, stack_pos);
  long hours = PopInt(op_stack, stack_pos);
  long year = PopInt(op_stack, stack_pos);
  long month = PopInt(op_stack, stack_pos);
  long day = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(instance) {
    // get current time
    time_t raw_time;
    time(&raw_time);  
    struct tm* curr_time;
    if(is_gmt) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // update time
    curr_time->tm_year = year - 1900;
    curr_time->tm_mon = month - 1;
    curr_time->tm_mday = day;
    curr_time->tm_hour = hours;
    curr_time->tm_min = mins;
    curr_time->tm_sec = secs;  
    mktime(curr_time);

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void TrapProcessor::ProcessSetTime3(long* &op_stack, long* &stack_pos)
{
}

/********************************
 * Get platform wstring
 ********************************/
void TrapProcessor::ProcessPlatform(StackProgram* program, long* &op_stack, long* &stack_pos) 
{
  wstring value_str = BytesToUnicode(System::GetPlatform());

  // create character array
  const long char_array_size = value_str.size();
  const long char_array_dim = 1;
  long* char_array = (long*)MemoryManager::AllocateArray(char_array_size + 1 +
																												 ((char_array_dim + 2) *
																													sizeof(long)),
																												 CHAR_ARY_TYPE,
																												 op_stack, *stack_pos, false);
  char_array[0] = char_array_size + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy wstring
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
  wcsncpy(char_array_ptr, value_str.c_str(), char_array_size);

  // create 'System.String' object instance
  long* str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(),
																								(long*)op_stack, *stack_pos, false);
  str_obj[0] = (long)char_array;
  str_obj[1] = char_array_size;

  PushInt((long)str_obj, op_stack, stack_pos);
}

//
// deserializes an array of objects
// 
inline long* TrapProcessor::DeserializeArray(ParamType type, long* inst, 
																						 long* &op_stack, long* &stack_pos) {
  if(!DeserializeByte(inst)) {
    return NULL;
  }
      
  long* src_array = (long*)inst[0];
  long dest_pos = inst[1];
      
  if(dest_pos < src_array[0]) {
    // TOOD: detect bad read?
    const long dest_array_size = DeserializeInt(inst);
    const long dest_array_dim = DeserializeInt(inst);
    const long dest_array_dim_size = DeserializeInt(inst);

    long* dest_array;
    if(type == BYTE_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size +
																											 ((dest_array_dim + 2) *
																												sizeof(long)),
																											 BYTE_ARY_TYPE,
																											 op_stack, *stack_pos,
																											 false);
    }
    else if(type == CHAR_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size +
																											 ((dest_array_dim + 2) *
																												sizeof(long)),
																											 CHAR_ARY_TYPE,
																											 op_stack, *stack_pos,
																											 false);
    }
    else if(type == INT_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size + dest_array_dim + 2, 
																											 INT_TYPE, op_stack, *stack_pos,
																											 false);
    }
    else {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size * 2 + dest_array_dim + 2, 
																											 INT_TYPE, op_stack, *stack_pos, false);
    }
    
    dest_array[0] = dest_array_size;
    dest_array[1] = dest_array_dim;
    dest_array[2] = dest_array_dim_size;	
	
    ReadSerializedBytes(dest_array, src_array, type, inst);	
    return dest_array;
  }
      
  return NULL;
}

//
// expand buffer
//
long* TrapProcessor::ExpandSerialBuffer(const long src_buffer_size, long* dest_buffer, long* inst, 
																				long* &op_stack, long* &stack_pos) {
  long dest_buffer_size = dest_buffer[2];
  const long dest_pos = inst[1];      
  const long calc_offset = src_buffer_size + dest_pos;
      
  if(calc_offset >= dest_buffer_size) {
    const long dest_pos = inst[1];
    while(calc_offset >= dest_buffer_size) {
      dest_buffer_size += calc_offset / 2;
    }
    // create byte array
    const long byte_array_size = dest_buffer_size;
    const long byte_array_dim = 1;
    long* byte_array = (long*)MemoryManager::AllocateArray(byte_array_size + 1 +
																													 ((byte_array_dim + 2) *
																														sizeof(long)),
																													 BYTE_ARY_TYPE,
																													 op_stack, *stack_pos,
																													 false);
    byte_array[0] = byte_array_size + 1;
    byte_array[1] = byte_array_dim;
    byte_array[2] = byte_array_size;
	
    // copy content
    char* byte_array_ptr = (char*)(byte_array + 3);
    const char* dest_buffer_ptr = (char*)(dest_buffer + 3);	
    memcpy(byte_array_ptr, dest_buffer_ptr, dest_pos);
	
    return byte_array;
  }

  return dest_buffer;
}

/********************************
 * Serializes an object graph
 ********************************/
void TrapProcessor::SerializeObject(long* inst, StackFrame* frame, long* &op_stack, long* &stack_pos)
{
  long* obj = (long*)frame->mem[1];
  ObjectSerializer serializer(obj);
  vector<char> src_buffer = serializer.GetValues();
  const long src_buffer_size = src_buffer.size();
  long* dest_buffer = (long*)inst[0];
  long dest_pos = inst[1];
  
  // expand buffer, if needed
  dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
  inst[0] = (long)dest_buffer;
  
  // copy content
  char* dest_buffer_ptr = ((char*)(dest_buffer + 3) + dest_pos);
  for(int i = 0; i < src_buffer_size; i++, dest_pos++) {
    dest_buffer_ptr[i] = src_buffer[i];
  }
  inst[1] = dest_pos;
}

/********************************
 * Deserializes an object graph
 ********************************/
void TrapProcessor::DeserializeObject(long* inst, long* &op_stack, long* &stack_pos) {
  if(!DeserializeByte(inst)) {
    PushInt(0, op_stack, stack_pos);    
  }
  else {
    long* byte_array = (long*)inst[0];
    const long dest_pos = inst[1];
    const long byte_array_dim_size = byte_array[2];  
    const char* byte_array_ptr = ((char*)(byte_array + 3) + dest_pos);
    
    ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
    PushInt((long)deserializer.DeserializeObject(), op_stack, stack_pos);
    inst[1] = dest_pos + deserializer.GetOffset();
  }
}

/********************************
 * Handles callback traps from 
 * the interpreter and JIT code
 ********************************/
bool TrapProcessor::ProcessTrap(StackProgram* program, long* inst, 
																long* &op_stack, long* &stack_pos, StackFrame* frame) {
  const long id = PopInt(op_stack, stack_pos);
  switch(id) {
    // ---------------- class instance operations ----------------
  case LOAD_CLS_INST_ID: {
    long* obj = (long*)PopInt(op_stack, stack_pos);
    PushInt(MemoryManager::GetObjectID(obj), op_stack, stack_pos);
  }
    break;

  case LOAD_NEW_OBJ_INST: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const wchar_t* name = (wchar_t*)(array + 3);
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_NEW_OBJ_INST; name='" << name << L"'"  << endl;
#endif
      CreateNewObject(name,  op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }    
  }
    break;

  case LOAD_CLS_BY_INST: {
#ifdef _DEBUG
    wcout << L"stack oper: LOAD_CLS_BY_INST" << endl;
#endif

    StackClass* cls = MemoryManager::GetClass(inst);
    if(!cls) {
      wcerr << L">>> Internal error: looking up class instance " << inst << L" <<<" << endl;
      return false;
    }
    // set name and create 'Class' instance
    long* cls_obj = MemoryManager::AllocateObject(program->GetClassObjectId(),
																									(long*)op_stack, *stack_pos, false);
    cls_obj[0] = (long)CreateStringObject(cls->GetName(), program, op_stack, stack_pos);
    frame->mem[1] = (long)cls_obj;
    CreateClassObject(cls, cls_obj, op_stack, stack_pos, program);
  }
    break;

    // ---------------- unicode operations ----------------
  case BYTES_TO_UNICODE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      return false;
    }
    const wstring out = BytesToUnicode((char*)(array + 3));
    
    // create character array
    const long char_array_size = out.size();
    const long char_array_dim = 1;
    long* char_array = (long*)MemoryManager::AllocateArray(char_array_size + 1 +
																													 ((char_array_dim + 2) *
																														sizeof(long)),
																													 CHAR_ARY_TYPE,
																													 op_stack, *stack_pos,
																													 false);
    char_array[0] = char_array_size + 1;
    char_array[1] = char_array_dim;
    char_array[2] = char_array_size;
    
    // copy wstring
    wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
    wcsncpy(char_array_ptr, out.c_str(), char_array_size);

    // push result
    PushInt((long)char_array, op_stack, stack_pos);
  }
    break;
    
  case UNICODE_TO_BYTES: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      return false;
    }
    const string out = UnicodeToBytes((wchar_t*)(array + 3));

    // create byte array
    const long byte_array_size = out.size();
    const long byte_array_dim = 1;
    long* byte_array = (long*)MemoryManager::AllocateArray(byte_array_size + 1 +
																													 ((byte_array_dim + 2) *
																														sizeof(long)),
																													 BYTE_ARY_TYPE,
																													 op_stack, *stack_pos,
																													 false);
    byte_array[0] = byte_array_size + 1;
    byte_array[1] = byte_array_dim;
    byte_array[2] = byte_array_size;
    
    // copy bytes
    char* byte_array_ptr = (char*)(byte_array + 3);
    strncpy(byte_array_ptr, out.c_str(), byte_array_size);
    
    // push result
    PushInt((long)byte_array, op_stack, stack_pos);
  }
    break;

    // ---------------- array operations ----------------    
  case LOAD_MULTI_ARY_SIZE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      return false;
    }
    
    // allocate 'size' array and copy metadata
    long size = array[1];
    long dim = 1;
    long* mem = (long*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE,
																										op_stack, *stack_pos);
    int i, j;
    for(i = 0, j = size + 2; i < size; i++) {
      mem[i + 3] = array[--j];
    }
    mem[0] = size;
    mem[1] = dim;
    mem[2] = size;

    PushInt((long)mem, op_stack, stack_pos);
  }
    break;
  
    // ---------------- memory copy operations ----------------
  case CPY_CHAR_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    const wchar_t* value_str = program->GetCharStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      return false;
    }
    const long size = array[2];
    wchar_t* str = (wchar_t*)(array + 3);
    memcpy(str, value_str, size * sizeof(wchar_t));
#ifdef _DEBUG
    wcout << L"stack oper: CPY_CHAR_STR_ARY: index=" << index << L", string='" << str << L"'" << endl;
#endif
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_CHAR_STR_ARYS: {
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      return false;
    }
    const long size = array[0];
    const long dim = array[1];
    // copy elements
    long* str = (long*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = PopInt(op_stack, stack_pos);
    }
#ifdef _DEBUG
    wcout << L"stack oper: CPY_CHAR_STR_ARYS" << endl;
#endif
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_INT_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    int* value_str = program->GetIntStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      return false;
    }
    const long size = array[0];
    const long dim = array[1];
    long* str = (long*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = value_str[i];
    }
#ifdef _DEBUG
    wcout << L"stack oper: CPY_INT_STR_ARY" << endl;
#endif
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_FLOAT_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    FLOAT_VALUE* value_str = program->GetFloatStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      return false;
    }
    const long size = array[0];
    const long dim = array[1];
    FLOAT_VALUE* str = (FLOAT_VALUE*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = value_str[i];
    }

#ifdef _DEBUG
    wcout << L"stack oper: CPY_FLOAT_STR_ARY" << endl;
#endif
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

    // ---------------- standard i/o ----------------
  case STD_OUT_BOOL:
#ifdef _DEBUG
    wcout << L"  STD_OUT_BOOL" << endl;
#endif
    wcout << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");
    break;
    
  case STD_OUT_BYTE:
#ifdef _DEBUG
    wcout << L"  STD_OUT_BYTE" << endl;
#endif
    wcout << (unsigned char)PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_CHAR:
#ifdef _DEBUG
    wcout << L"  STD_OUT_CHAR" << endl;
#endif
    wcout << (wchar_t)PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_INT:
#ifdef _DEBUG
    wcout << L"  STD_OUT_INT" << endl;
#endif
    wcout << PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_FLOAT:
#ifdef _DEBUG
    wcout << L"  STD_OUT_FLOAT" << endl;
#endif
    wcout.precision(9);
    wcout << PopFloat(op_stack, stack_pos);
    break;
    
  case STD_OUT_CHAR_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
#ifdef _DEBUG
    wcout << L"  STD_OUT_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif
    
    if(array) {
      wchar_t* str = (wchar_t*)(array + 3);
      wcout << str;
    }
    else {
      wcout << L"Nil";
    }
  }
    break;

  case STD_OUT_BYTE_ARY_LEN: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const size_t num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
    wcout << L"  STD_OUT_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

    if(array && offset > -1 && offset + num <= (size_t)array[2]) {
      const char* buffer = (char*)(array + 3);
      for(size_t i = 0; i < num; i++) {
        wcout << (char)buffer[i + offset];
      }
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      wcout << L"Nil";
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
    
  case STD_OUT_CHAR_ARY_LEN: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const size_t num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
    wcout << L"  STD_OUT_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

    if(array && offset > -1 && offset + num <= (size_t)array[2]) {
      const wchar_t* buffer = (wchar_t*)(array + 3);
      wcout.write(buffer + offset, num);
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      wcout << L"Nil";
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case STD_IN_STRING: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      // read input
      const long max = array[2];
      wchar_t* buffer = new wchar_t[max];
      
      wcin.getline(buffer, max);
#ifdef _WIN32
      if(wcin.peek() == L'\n') {
        wcin.get();
      }
#endif
      
      wchar_t* dest = (wchar_t*)(array + 3);
      wcsncpy(dest, buffer, max - 1);
      // clean up
      delete[] buffer;
      buffer = NULL;
    }
  }
    break;
    
    // ---------------- standard error i/o ----------------
  case STD_ERR_BOOL:
#ifdef _DEBUG
    wcout << L"  STD_ERR_BOOL" << endl;
#endif
    wcerr << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");
    break;
    
  case STD_ERR_BYTE:
#ifdef _DEBUG
    wcout << L"  STD_ERR_BYTE" << endl;
#endif
    wcerr << (unsigned char)PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_CHAR:
#ifdef _DEBUG
    wcout << L"  STD_ERR_CHAR" << endl;
#endif
    wcerr << (char)PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_INT:
#ifdef _DEBUG
    wcout << L"  STD_ERR_INT" << endl;
#endif
    wcerr << PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_FLOAT:
#ifdef _DEBUG
    wcout << L"  STD_ERR_FLOAT" << endl;
#endif
    wcerr.precision(9);
    wcerr << PopFloat(op_stack, stack_pos);
    break;
    
  case STD_ERR_CHAR_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    
#ifdef _DEBUG
    wcout << L"  STD_ERR_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif
    
    if(array) {
      const wchar_t* str = (wchar_t*)(array + 3);
      wcerr << str;
    }
    else {
      wcerr << L"Nil";
    }
  }
    break;

  case STD_ERR_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const size_t num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
    wcout << L"  STD_ERR_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

    if(array && offset > -1 && offset + num <= (size_t)array[2]) {
      const unsigned char* buffer = (unsigned char*)(array + 3);
      for(size_t i = 0; i < num; i++) {
        wcerr << (char)buffer[i + offset];
      }
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      wcerr << L"Nil";
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

    // ---------------- runtime ----------------
  case EXIT:
    exit(PopInt(op_stack, stack_pos));
    break;

  case GMT_TIME:
    ProcessCurrentTime(frame, true);
    break;

  case SYS_TIME:
    ProcessCurrentTime(frame, false);
    break;

  case DATE_TIME_SET_1:
    ProcessSetTime1(op_stack, stack_pos);
    break;

  case DATE_TIME_SET_2:
    ProcessSetTime2(op_stack, stack_pos);
    break;
    
  case DATE_TIME_ADD_DAYS:
    ProcessAddTime(DAY_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_HOURS:
    ProcessAddTime(HOUR_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_MINS:
    ProcessAddTime(MIN_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_SECS:
    ProcessAddTime(SEC_TIME, op_stack, stack_pos);
    break;

  case PLTFRM:
    ProcessPlatform(program, op_stack, stack_pos);
    break;
    
  case GET_SYS_PROP: {
    long* key_array = (long*)PopInt(op_stack, stack_pos);
    if(key_array) {    
      key_array = (long*)key_array[0];
      const wchar_t* key = (wchar_t*)(key_array + 3);
      long* value = CreateStringObject(program->GetProperty(key), program, op_stack, stack_pos);
      PushInt((long)value, op_stack, stack_pos);
    }
    else {
      long* value = CreateStringObject(L"", program, op_stack, stack_pos);
      PushInt((long)value, op_stack, stack_pos);
    }
  }
    break;
    
  case SET_SYS_PROP: {
    long* value_array = (long*)PopInt(op_stack, stack_pos);
    long* key_array = (long*)PopInt(op_stack, stack_pos);
    
    if(key_array && value_array) {    
      value_array = (long*)value_array[0];
      key_array = (long*)key_array[0];
      
      const wchar_t* key = (wchar_t*)(key_array + 3);
      const wchar_t* value = (wchar_t*)(value_array + 3);
      program->SetProperty(key, value);
    }
  }
    break;
    
    // ---------------- ip socket i/o ----------------
  case SOCK_TCP_HOST_NAME: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {    
      const long size = array[2];
      wchar_t* str = (wchar_t*)(array + 3);

      // get host name
      char buffer[SMALL_BUFFER_MAX + 1];    
      if(!gethostname(buffer, SMALL_BUFFER_MAX)) {
        // copy name   
				long i = 0;
        for(; buffer[i] != L'\0' && i < size; i++) {
          str[i] = buffer[i];
        }
				str[i] = L'\0';
      }
      else {
        str[0] = L'\0';
      }
#ifdef _DEBUG
      wcout << L"stack oper: SOCK_TCP_HOST_NAME: host='" << str << L"'" << endl;
#endif
      PushInt((long)array, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
    
  case SOCK_TCP_CONNECT: {
    long port = PopInt(op_stack, stack_pos);
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(array && instance) {
      array = (long*)array[0];
      const wstring waddr((wchar_t*)(array + 3));
      const string addr(waddr.begin(), waddr.end());
      SOCKET sock = IPSocket::Open(addr.c_str(), port);
#ifdef _DEBUG
      wcout << L"# socket connect: addr='" << waddr << L":" << port << L"'; instance=" 
						<< instance << L"(" << (long)instance << L")" << L"; addr=" << sock << L"(" 
						<< (long)sock << L") #" << endl;
#endif
      instance[0] = (long)sock;
    }
  }
    break;  

  case SOCK_TCP_BIND: {
    long port = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance) {
      SOCKET server = IPSocket::Bind(port);
#ifdef _DEBUG
      wcout << L"# socket bind: port=" << port << L"; instance=" << instance << L"(" 
						<< (long)instance << L")" << L"; addr=" << server << L"(" << (long)server 
						<< L") #" << endl;
#endif
      instance[0] = (long)server;
    }
  }
    break;  

  case SOCK_TCP_LISTEN: {
    long backlog = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);

#ifdef _WIN32
    if(instance && (SOCKET)instance[0] != INVALID_SOCKET) {
#else
      if(instance && (SOCKET)instance[0] > -1) {
#endif
				SOCKET server = (SOCKET)instance[0];
#ifdef _DEBUG
				wcout << L"# socket listen: backlog=" << backlog << L"'; instance=" << instance 
							<< L"(" << (long)instance << L")" << L"; addr=" << server << L"(" 
							<< (long)server << L") #" << endl;
#endif
				if(IPSocket::Listen(server, backlog)) {
					PushInt(1, op_stack, stack_pos);    
				}
				else {
					PushInt(0, op_stack, stack_pos);
				}
      }
      else {
				PushInt(0, op_stack, stack_pos);
      }
    }
    break;   
    
    case SOCK_TCP_ACCEPT: {
      long* instance = (long*)PopInt(op_stack, stack_pos);
#ifdef _WIN32
      if(instance && (SOCKET)instance[0] != INVALID_SOCKET) {
#else
				if(instance && (SOCKET)instance[0] > -1) {
#endif
					SOCKET server = (SOCKET)instance[0];
					char client_address[SMALL_BUFFER_MAX + 1];
					int client_port;
					SOCKET client = IPSocket::Accept(server, client_address, client_port);
#ifdef _DEBUG
					wcout << L"# socket accept: instance=" << instance << L"(" << (long)instance << L")" << L"; ip=" 
								<< BytesToUnicode(client_address) << L"; port=" << client_port << L"; addr=" << server << L"(" 
								<< (long)server << L") #" << endl;
#endif
					const wstring wclient_address = BytesToUnicode(client_address);
					long* sock_obj = MemoryManager::AllocateObject(program->GetSocketObjectId(),
																												 (long*)op_stack, *stack_pos, false);
					sock_obj[0] = client;
					sock_obj[1] = (long)CreateStringObject(wclient_address, program, op_stack, stack_pos);
					sock_obj[2] = client_port;
      
					PushInt((long)sock_obj, op_stack, stack_pos);
				}
      }
      break;
    
    case SOCK_TCP_CLOSE: {
      long* instance = (long*)PopInt(op_stack, stack_pos);
#ifdef _WIN32
      if(instance && (SOCKET)instance[0] != INVALID_SOCKET) {
#else
				if(instance && (SOCKET)instance[0] > -1) {
#endif
					SOCKET sock = (SOCKET)instance[0];
	
#ifdef _DEBUG
					wcout << L"# socket close: addr=" << sock << L"(" << (long)sock << L") #" << endl;
#endif	
					instance[0] = 0;
					IPSocket::Close(sock);
				}      
      }
      break;
    
      case SOCK_TCP_OUT_STRING: {
				long* array = (long*)PopInt(op_stack, stack_pos);
				long* instance = (long*)PopInt(op_stack, stack_pos);
				if(array && instance) {
					SOCKET sock = (SOCKET)instance[0];
					const wchar_t* wdata = (wchar_t*)(array + 3); 
	
#ifdef _DEBUG
					wcout << L"# socket write string: instance=" << instance << L"(" << (long)instance << L")" 
								<< L"; array=" << array << L"(" << (long)array << L")" << L"; data=" << wdata << endl;
#endif	
   
#ifdef _WIN32
					if(sock != INVALID_SOCKET) {
#else
						if(sock > -1) {
#endif
							const string data = UnicodeToBytes(wdata);
							IPSocket::WriteBytes(data.c_str(), data.size(), sock);
						}
					}
				}
				break;
      
      case SOCK_TCP_IN_STRING: {
				long* array = (long*)PopInt(op_stack, stack_pos);
				long* instance = (long*)PopInt(op_stack, stack_pos);
				if(array && instance) {
					char buffer[SMALL_BUFFER_MAX + 1];
					SOCKET sock = (SOCKET)instance[0];	
					int status;
#ifdef _WIN32
					if(sock != INVALID_SOCKET) {
#else
						if(sock > -1) {
#endif
							int index = 0;
							char value;
							bool end_line = false;
							do {
								value = IPSocket::ReadByte(sock, status);
								if(value != '\r' && value != '\n' && index < SMALL_BUFFER_MAX && status > 0) {
									buffer[index++] = value;
								}
								else {
									end_line = true;
								}
							}
							while(!end_line);
							buffer[index] = '\0';
	  
							// assume LF
							if(value == '\r') {
								IPSocket::ReadByte(sock, status);
							}
	  
							// copy content
							const wstring in = BytesToUnicode(buffer);	      
							wchar_t* out = (wchar_t*)(array + 3);
							const long max = array[2];
							wcsncpy(out, in.c_str(), max);
						}
					}
				}
				break;
      
				// ---------------- secure ip socket i/o ----------------
				case SOCK_TCP_SSL_CONNECT: {
					const long port = PopInt(op_stack, stack_pos);
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						array = (long*)array[0];
						const wstring waddr((wchar_t*)(array + 3));
						const string addr(waddr.begin(), waddr.end());
	
						SSL_CTX* ctx; BIO* bio;
						instance[2] = IPSecureSocket::Open(addr.c_str(), port, ctx, bio);
						instance[0] = (long)ctx;
						instance[1] = (long)bio;
#ifdef _DEBUG
						wcout << L"# socket connect: addr='" << waddr << L":" << port << L"'; instance="
									<< instance << L"(" << (long)instance << L")" << L"; addr=" << ctx << L"|" << bio << L"(" 
									<< (long)ctx << L"|"  << (long)bio << L") #" << endl;
#endif
					}
				}
					break;  
      
				case SOCK_TCP_SSL_CLOSE: {
					long* instance = (long*)PopInt(op_stack, stack_pos);    
					SSL_CTX* ctx = (SSL_CTX*)instance[0];
					BIO* bio = (BIO*)instance[1];
      
#ifdef _DEBUG
					wcout << L"# socket close: addr=" << ctx << L"|" << bio << L"(" 
								<< (long)ctx << L"|"  << (long)bio << L") #" << endl;
#endif      
					IPSecureSocket::Close(ctx, bio);    
					instance[0] = instance[1] = instance[2] = 0;      
				}
					break;
      
				case SOCK_TCP_SSL_OUT_STRING: {
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						SSL_CTX* ctx = (SSL_CTX*)instance[0];
						BIO* bio = (BIO*)instance[1];      
						const wstring data((wchar_t*)(array + 3));
						if(instance[2]) {
							const string out = UnicodeToBytes(data);
							IPSecureSocket::WriteBytes(out.c_str(), out.size(), ctx, bio);
						}
					}
				}
					break;
      
				case SOCK_TCP_SSL_IN_STRING: {
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						char buffer[SMALL_BUFFER_MAX + 1];
						SSL_CTX* ctx = (SSL_CTX*)instance[0];
						BIO* bio = (BIO*)instance[1]; 
						int status;
						if(instance[2]) {
							int index = 0;
							char value;
							bool end_line = false;
							do {
								value = IPSecureSocket::ReadByte(ctx, bio, status);
								if(value != '\r' && value != '\n' && index < SMALL_BUFFER_MAX && status > 0) {
									buffer[index++] = value;
								}
								else {
									end_line = true;
								}
							}
							while(!end_line);
							buffer[index] = '\0';
	  
							// assume LF
							if(value == '\r') {
								IPSecureSocket::ReadByte(ctx, bio, status);
							}
	  
							// copy content
							const wstring in = BytesToUnicode(buffer);	      
							wchar_t* out = (wchar_t*)(array + 3);
							const long max = array[2];
							wcsncpy(out, in.c_str(), max);
						}
					}
				}
					break;
	  
					// ---------------- serialization ----------------
				case SERL_CHAR:
#ifdef _DEBUG
					wcout << L"# serializing char #" << endl;
#endif
					SerializeInt(CHAR_PARM, inst, op_stack, stack_pos);
					SerializeChar((wchar_t)frame->mem[1], inst, op_stack, stack_pos);
					break;
	  
				case SERL_INT:
#ifdef _DEBUG
					wcout << L"# serializing int #" << endl;
#endif
					SerializeInt(INT_PARM, inst, op_stack, stack_pos);
					SerializeInt(frame->mem[1], inst, op_stack, stack_pos);
					break;
	
				case SERL_FLOAT: {
#ifdef _DEBUG
					wcout << L"# serializing float #" << endl;
#endif
					SerializeInt(FLOAT_PARM, inst, op_stack, stack_pos);
					FLOAT_VALUE value;
					memcpy(&value, &(frame->mem[1]), sizeof(value));
					SerializeFloat(value, inst, op_stack, stack_pos);
				}
					break;

				case SERL_OBJ_INST:
					SerializeObject(inst, frame, op_stack, stack_pos);
					break;

				case SERL_BYTE_ARY:
					SerializeInt(BYTE_ARY_PARM, inst, op_stack, stack_pos);
					SerializeArray((long*)frame->mem[1], BYTE_ARY_PARM, inst, op_stack, stack_pos);
					break;

				case SERL_CHAR_ARY:
					SerializeInt(CHAR_ARY_PARM, inst, op_stack, stack_pos);
					SerializeArray((long*)frame->mem[1], CHAR_ARY_PARM, inst, op_stack, stack_pos);
					break;
	  
				case SERL_INT_ARY:
					SerializeInt(INT_ARY_PARM, inst, op_stack, stack_pos);
					SerializeArray((long*)frame->mem[1], INT_ARY_PARM, inst, op_stack, stack_pos);
					break;

				case SERL_FLOAT_ARY:
					SerializeInt(FLOAT_ARY_PARM, inst, op_stack, stack_pos);
					SerializeArray((long*)frame->mem[1], FLOAT_ARY_PARM, inst, op_stack, stack_pos);
					break;

				case DESERL_CHAR:
#ifdef _DEBUG
					wcout << L"# deserializing char #" << endl;
#endif
					if(CHAR_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt(DeserializeChar(inst), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;
	  
				case DESERL_INT:
#ifdef _DEBUG
					wcout << L"# deserializing int #" << endl;
#endif
					if(INT_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt(DeserializeInt(inst), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;

				case DESERL_FLOAT:
#ifdef _DEBUG
					wcout << L"# deserializing float #" << endl;
#endif
					if(FLOAT_PARM == (ParamType)DeserializeInt(inst)) {
						PushFloat(DeserializeFloat(inst), op_stack, stack_pos);
					}
					else {
						PushFloat(0.0, op_stack, stack_pos);
					}
					break;

				case DESERL_OBJ_INST:
					DeserializeObject(inst, op_stack, stack_pos);
					break;

				case DESERL_BYTE_ARY:
#ifdef _DEBUG
					wcout << L"# deserializing byte array #" << endl;
#endif
					if(BYTE_ARY_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt((long)DeserializeArray(BYTE_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;
	  
				case DESERL_CHAR_ARY:
#ifdef _DEBUG
					wcout << L"# deserializing char array #" << endl;
#endif
					if(CHAR_ARY_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt((long)DeserializeArray(CHAR_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;
	  
				case DESERL_INT_ARY:
#ifdef _DEBUG
					wcout << L"# deserializing int array #" << endl;
#endif
					if(INT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt((long)DeserializeArray(INT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;

				case DESERL_FLOAT_ARY:
#ifdef _DEBUG
					wcout << L"# deserializing float array #" << endl;
#endif
					if(FLOAT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
						PushInt((long)DeserializeArray(FLOAT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
					}
					else {
						PushInt(0, op_stack, stack_pos);
					}
					break;

					// ---------------- file i/o ----------------
				case FILE_OPEN_READ: {
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						array = (long*)array[0];
						const wstring name((wchar_t*)(array + 3));
						const string filename(name.begin(), name.end());
						FILE* file = File::FileOpen(filename.c_str(), "rb");
#ifdef _DEBUG
						wcout << L"# file open: name='" << name << L"'; instance=" << instance << L"(" 
									<< (long)instance << L")" << L"; addr=" << file << L"(" << (long)file 
									<< L") #" << endl;
#endif
						instance[0] = (long)file;
					}
				}
					break;
      
				case FILE_OPEN_WRITE: {
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						array = (long*)array[0];
						const wstring name((wchar_t*)(array + 3));
						const string filename(name.begin(), name.end());
						FILE* file = File::FileOpen(filename.c_str(), "wb");
#ifdef _DEBUG
						wcout << L"# file open: name='" << name << L"'; instance=" << instance << L"(" 
									<< (long)instance << L")" << L"; addr=" << file << L"(" << (long)file 
									<< L") #" << endl;
#endif
						instance[0] = (long)file;
					}
				}
					break;
      
				case FILE_OPEN_READ_WRITE: {
					long* array = (long*)PopInt(op_stack, stack_pos);
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {
						array = (long*)array[0];
						const wstring name((wchar_t*)(array + 3));
						const string filename(name.begin(), name.end());
						FILE* file = File::FileOpen(filename.c_str(), "w+b");
#ifdef _DEBUG
						wcout << L"# file open: name='" << name << L"'; instance=" << instance << L"(" 
									<< (long)instance << L")" << L"; addr=" << file << L"(" << (long)file 
									<< L") #" << endl;
#endif
						instance[0] = (long)file;
					}
				}
					break;
      
				case FILE_CLOSE: {
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(instance && (FILE*)instance[0]) {
						FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
						wcout << L"# file close: addr=" << file << L"(" << (long)file << L") #" << endl;
#endif
						instance[0] = 0;
						fclose(file);
					}
				}
					break;
      
				case FILE_FLUSH: {
					long* instance = (long*)PopInt(op_stack, stack_pos);
					if(instance && (FILE*)instance[0]) {
						FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
						wcout << L"# file close: addr=" << file << L"(" << (long)file << L") #" << endl;
#endif
						instance[0] = 0;
						fflush(file);
					}
				}
					break;
	
				case FILE_IN_STRING: {
					const long* array = (long*)PopInt(op_stack, stack_pos);
					const long* instance = (long*)PopInt(op_stack, stack_pos);
					if(array && instance) {	    
						FILE* file = (FILE*)instance[0];
						char buffer[SMALL_BUFFER_MAX + 1];
						if(file && fgets(buffer, SMALL_BUFFER_MAX, file)) {
							long end_index = strlen(buffer) - 1;
							if(end_index > -1) {
								if(buffer[end_index] == '\n') {
									buffer[end_index] = '\0';
								}
							}
							else {
								buffer[0] = '\0';
							}
							// copy
							const wstring in = BytesToUnicode(buffer);	      
							wchar_t* out = (wchar_t*)(array + 3);
							const long max = array[2]; 
							wcsncpy(out, in.c_str(), max);
						}
					}
				}
					break;
      
				case FILE_OUT_STRING: {
					const long* array = (long*)PopInt(op_stack, stack_pos);
					const long* instance = (long*)PopInt(op_stack, stack_pos);    
					if(array && instance) {
						FILE* file = (FILE*)instance[0];
						const wchar_t* data = (wchar_t*)(array + 3);      
						if(file) {
							fputs(UnicodeToBytes(data).c_str(), file);
						}
					}
				}
					break;
	
				case FILE_REWIND: {
					const long* instance = (long*)PopInt(op_stack, stack_pos);
					if(instance && (FILE*)instance[0]) {
						FILE* file = (FILE*)instance[0];
						rewind(file);
					}
				}
					break;
      
					// ---------------- socket i/o ----------------
				case SOCK_TCP_IS_CONNECTED: {
					long* instance = (long*)PopInt(op_stack, stack_pos);
#ifdef _WIN32
					if(instance && (SOCKET)instance[0] != INVALID_SOCKET) {
#else
						if(instance && (SOCKET)instance[0] > -1) {
#endif
							PushInt(1, op_stack, stack_pos);
						} 
						else {
							PushInt(0, op_stack, stack_pos);
						}
					}
					break;
      
					case SOCK_TCP_IN_BYTE: {
						long* instance = (long*)PopInt(op_stack, stack_pos);
						if(instance) {
							SOCKET sock = (SOCKET)instance[0];
							int status;
							PushInt(IPSocket::ReadByte(sock, status), op_stack, stack_pos);
						}
						else {
							PushInt(0, op_stack, stack_pos);
						}
					}
						break;
      
					case SOCK_TCP_IN_BYTE_ARY: {
						long* array = (long*)PopInt(op_stack, stack_pos);
						const long num = PopInt(op_stack, stack_pos);
						const long offset = PopInt(op_stack, stack_pos);
						long* instance = (long*)PopInt(op_stack, stack_pos);
      
#ifdef _WIN32    
						if(array && instance && (SOCKET)instance[0] != INVALID_SOCKET && offset + num < array[0]) {
#else
							if(array && instance && (SOCKET)instance[0] > -1 && offset + num < array[0]) {
#endif
								SOCKET sock = (SOCKET)instance[0];
								char* buffer = (char*)(array + 3);
								PushInt(IPSocket::ReadBytes(buffer + offset, num, sock), op_stack, stack_pos);
							}
							else {
								PushInt(-1, op_stack, stack_pos);
							}
						}
						break;
      
						case SOCK_TCP_OUT_BYTE: {
							long value = PopInt(op_stack, stack_pos);
							long* instance = (long*)PopInt(op_stack, stack_pos);
							if(instance) {
								SOCKET sock = (SOCKET)instance[0];
								IPSocket::WriteByte((char)value, sock);
								PushInt(1, op_stack, stack_pos);
							}
							else {
								PushInt(0, op_stack, stack_pos);
							}
						}
							break;
      
						case SOCK_TCP_OUT_BYTE_ARY: {
							long* array = (long*)PopInt(op_stack, stack_pos);
							const long num = PopInt(op_stack, stack_pos);
							const long offset = PopInt(op_stack, stack_pos);
							long* instance = (long*)PopInt(op_stack, stack_pos);
      
#ifdef _WIN32
							if(array && instance && (SOCKET)instance[0] != INVALID_SOCKET && offset + num < array[0]) {
#else
								if(array && instance && (SOCKET)instance[0] > -1 && offset + num < array[0]) {
#endif
									SOCKET sock = (SOCKET)instance[0];
									char* buffer = (char*)(array + 3);
									PushInt(IPSocket::WriteBytes(buffer + offset, num, sock), op_stack, stack_pos);
								} 
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							} 
							break;
      
							// ---------------- secure socket i/o ----------------
							case SOCK_TCP_SSL_IN_BYTE: {
								long* instance = (long*)PopInt(op_stack, stack_pos);
								if(instance) {
									SSL_CTX* ctx = (SSL_CTX*)instance[0];
									BIO* bio = (BIO*)instance[1];      
									int status;
									PushInt(IPSecureSocket::ReadByte(ctx, bio, status), op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
	      
							case SOCK_TCP_SSL_IN_BYTE_ARY: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								const long num = PopInt(op_stack, stack_pos);
								const long offset = PopInt(op_stack, stack_pos);
								long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(array && instance && instance[2] && offset + num <= array[2]) {
									SSL_CTX* ctx = (SSL_CTX*)instance[0];
									BIO* bio = (BIO*)instance[1];
									char* buffer = (char*)(array + 3);
									PushInt(IPSecureSocket::ReadBytes(buffer + offset, num, ctx, bio), op_stack, stack_pos);
								}
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							}
								break;
      
							case SOCK_TCP_SSL_OUT_BYTE: {
								long value = PopInt(op_stack, stack_pos);
								long* instance = (long*)PopInt(op_stack, stack_pos);
								if(instance) {
									SSL_CTX* ctx = (SSL_CTX*)instance[0];
									BIO* bio = (BIO*)instance[1];
									IPSecureSocket::WriteByte((char)value, ctx, bio);
									PushInt(1, op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case SOCK_TCP_SSL_OUT_BYTE_ARY: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								const long num = PopInt(op_stack, stack_pos);
								const long offset = PopInt(op_stack, stack_pos);
								long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(array && instance && instance[2] && offset + num <= array[2]) {
									SSL_CTX* ctx = (SSL_CTX*)instance[0];
									BIO* bio = (BIO*)instance[1];
									char* buffer = (char*)(array + 3);
									PushInt(IPSecureSocket::WriteBytes(buffer + offset, num, ctx, bio), op_stack, stack_pos);
								} 
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							} 
								break;

								// -------------- file i/o -----------------
							case FILE_IN_BYTE: {
								const long* instance = (long*)PopInt(op_stack, stack_pos);
								if((FILE*)instance[0]) {
									FILE* file = (FILE*)instance[0];
									if(fgetc(file) == EOF) {
										PushInt(0, op_stack, stack_pos);
									} 
									else {
										PushInt(1, op_stack, stack_pos);
									}
								} 
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_IN_CHAR_ARY: {
								const long* array = (long*)PopInt(op_stack, stack_pos);
								const long num = PopInt(op_stack, stack_pos);
								const long offset = PopInt(op_stack, stack_pos);
								const long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[2]) {
									FILE* file = (FILE*)instance[0];
									wchar_t* out = (wchar_t*)(array + 3);

									// read from file
									char* byte_buffer = new char[num];
									const size_t max = fread(byte_buffer + offset, 1, num, file);
									byte_buffer[max] = '\0';
									const wstring in(BytesToUnicode(byte_buffer));

									// clean up
									delete[] byte_buffer;
									byte_buffer = NULL;
	
									// copy
									wcsncpy(out, in.c_str(), array[2]);	
									PushInt(max, op_stack, stack_pos);	
								} 
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_IN_BYTE_ARY: {
								const long* array = (long*)PopInt(op_stack, stack_pos);
								const long num = PopInt(op_stack, stack_pos);
								const long offset = PopInt(op_stack, stack_pos);
								const long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[2]) {
									FILE* file = (FILE*)instance[0];
									char* buffer = (char*)(array + 3);
									PushInt(fread(buffer + offset, 1, num, file), op_stack, stack_pos);     
								} 
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_OUT_BYTE: {
								const long value = PopInt(op_stack, stack_pos);
								const long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(instance && (FILE*)instance[0]) {
									FILE* file = (FILE*)instance[0];
									if(fputc(value, file) != value) {
										PushInt(0, op_stack, stack_pos);
									} 
									else {
										PushInt(1, op_stack, stack_pos);
									}
	
								} 
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_OUT_BYTE_ARY: {
								const long* array = (long*)PopInt(op_stack, stack_pos);
								const long num = PopInt(op_stack, stack_pos);
								const long offset = PopInt(op_stack, stack_pos);
								const long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(array && instance && (FILE*)instance[0] && offset >=0 && offset + num <= array[2]) {
									FILE* file = (FILE*)instance[0];
									char* buffer = (char*)(array + 3);
									PushInt(fwrite(buffer + offset, 1, num, file), op_stack, stack_pos);
								} 
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_SEEK: {
								long pos = PopInt(op_stack, stack_pos);
								long* instance = (long*)PopInt(op_stack, stack_pos);
      
								if(instance && (FILE*)instance[0]) {
									FILE* file = (FILE*)instance[0];
									if(fseek(file, pos, SEEK_CUR) != 0) {
										PushInt(0, op_stack, stack_pos);
									} 
									else {
										PushInt(1, op_stack, stack_pos);
									}
								} 
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_EOF: {
								const long* instance = (long*)PopInt(op_stack, stack_pos);
								if(instance && (FILE*)instance[0]) {
									FILE* file = (FILE*)instance[0];
									PushInt(feof(file) != 0, op_stack, stack_pos);
								} 
								else {
									PushInt(1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_IS_OPEN: {
								const long* instance = (long*)PopInt(op_stack, stack_pos);
								if(instance && (FILE*)instance[0]) {
									PushInt(1, op_stack, stack_pos);
								} 
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_EXISTS: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									PushInt(File::FileExists(name.c_str()), op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_SIZE: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									PushInt(File::FileSize(name.c_str()), op_stack, stack_pos);
								}
								else {
									PushInt(-1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_DELETE: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									if(remove(name.c_str()) != 0) {
										PushInt(0, op_stack, stack_pos);
									} 
									else {
										PushInt(1, op_stack, stack_pos);
									}
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_RENAME: {
								const long* to = (long*)PopInt(op_stack, stack_pos);
								const long* from = (long*)PopInt(op_stack, stack_pos);
      
								if(!to || !from) {
									PushInt(0, op_stack, stack_pos);
									return true;
								}
      
								to = (long*)to[0];
								const wstring wto_name((wchar_t*)(to + 3));
      
								from = (long*)from[0];
								const wstring wfrom_name((wchar_t*)(from + 3));
      
								const string to_name(wto_name.begin(), wto_name.end());
								const string from_name(wfrom_name.begin(), wfrom_name.end());
								if(rename(from_name.c_str(), to_name.c_str()) != 0) {
									PushInt(0, op_stack, stack_pos);
								} 
								else {
									PushInt(1, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_CREATE_TIME: {
								const long is_gmt = PopInt(op_stack, stack_pos);
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());	
									time_t raw_time = File::FileCreatedTime(name.c_str());      
									if(raw_time > 0) {
										struct tm* curr_time;
										if(is_gmt) {
											curr_time = gmtime(&raw_time);
										}
										else {
											curr_time = localtime(&raw_time);
										}
	  
										frame->mem[3] = curr_time->tm_mday;          // day
										frame->mem[4] = curr_time->tm_mon + 1;       // month
										frame->mem[5] = curr_time->tm_year + 1900;   // year
										frame->mem[6] = curr_time->tm_hour;          // hours
										frame->mem[7] = curr_time->tm_min;           // mins
										frame->mem[8] = curr_time->tm_sec;           // secs
									}
									else {
										PushInt(0, op_stack, stack_pos);
									}
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_MODIFIED_TIME: {
								const long is_gmt = PopInt(op_stack, stack_pos);
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());	
									time_t raw_time = File::FileModifiedTime(name.c_str());      
									if(raw_time > 0) {
										struct tm* curr_time;
										if(is_gmt) {
											curr_time = gmtime(&raw_time);
										}
										else {
											curr_time = localtime(&raw_time);
										}
	  
										frame->mem[3] = curr_time->tm_mday;          // day
										frame->mem[4] = curr_time->tm_mon + 1;       // month
										frame->mem[5] = curr_time->tm_year + 1900;   // year
										frame->mem[6] = curr_time->tm_hour;          // hours
										frame->mem[7] = curr_time->tm_min;           // mins
										frame->mem[8] = curr_time->tm_sec;           // secs
									}
									else {
										PushInt(0, op_stack, stack_pos);
									}
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case FILE_ACCESSED_TIME: {
								const long is_gmt = PopInt(op_stack, stack_pos);
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());	
									time_t raw_time = File::FileAccessedTime(name.c_str());      
									if(raw_time > 0) {
										struct tm* curr_time;
										if(is_gmt) {
											curr_time = gmtime(&raw_time);
										}
										else {
											curr_time = localtime(&raw_time);
										}
	  
										frame->mem[3] = curr_time->tm_mday;          // day
										frame->mem[4] = curr_time->tm_mon + 1;       // month
										frame->mem[5] = curr_time->tm_year + 1900;   // year
										frame->mem[6] = curr_time->tm_hour;          // hours
										frame->mem[7] = curr_time->tm_min;           // mins
										frame->mem[8] = curr_time->tm_sec;           // secs
									}
									else {
										PushInt(0, op_stack, stack_pos);
									}
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
								//----------- directory functions -----------
							case DIR_CREATE: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									PushInt(File::MakeDir(name.c_str()), op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case DIR_EXISTS: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								if(array) {
									array = (long*)array[0];
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									PushInt(File::IsDir(name.c_str()), op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
      
							case DIR_LIST: {
								long* array = (long*)PopInt(op_stack, stack_pos);
								array = (long*)array[0];
								if(array) {
									const wstring wname((wchar_t*)(array + 3));
									const string name(wname.begin(), wname.end());
									vector<string> files = File::ListDir(name.c_str());
	  
									// create 'System.String' object array
									const long str_obj_array_size = files.size();
									const long str_obj_array_dim = 1;
									long* str_obj_array = (long*)MemoryManager::AllocateArray(str_obj_array_size +
																																						str_obj_array_dim + 2,
																																						INT_TYPE, op_stack,
																																						*stack_pos, false);
									str_obj_array[0] = str_obj_array_size;
									str_obj_array[1] = str_obj_array_dim;
									str_obj_array[2] = str_obj_array_size;
									long* str_obj_array_ptr = str_obj_array + 3;
	
									// create and assign 'System.String' instances to array
									for(size_t i = 0; i < files.size(); ++i) {
										const wstring wfile(files[i].begin(), files[i].end());
										str_obj_array_ptr[i] = (long)CreateStringObject(wfile, program, op_stack, stack_pos);
									}
	
									PushInt((long)str_obj_array, op_stack, stack_pos);
								}
								else {
									PushInt(0, op_stack, stack_pos);
								}
							}
								break;
  
#ifdef _DEBUG    
							default:
								wcerr << L">>> internal error: Unhandled TRAP <<<" << endl;
								exit(1);
#endif
						}
      
							return true;
					}

#endif
