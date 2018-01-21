/***************************************************************************
 * VM common.
 *
 * Copyright (c) 2008-2018, Randy Hollines
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
#ifndef _SCRIPTER
#include "loader.h"
#endif
#include "interpreter.h"
#include "../shared/version.h"

#ifdef _WIN32
#ifndef _UTILS
#include "arch/win32/windows.h"
#endif
#include "arch/win32/memory.h"
#else
#include "arch/posix/posix.h"
#include "arch/posix/memory.h"
#endif

#ifdef _WIN32
CRITICAL_SECTION StackProgram::program_cs;
CRITICAL_SECTION StackMethod::virutal_cs;
CRITICAL_SECTION StackProgram::prop_cs;
// support VS2015
#if _MSC_VER >= 1900
extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }
#endif
#else
pthread_mutex_t StackProgram::program_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackMethod::virtual_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackProgram::prop_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

unordered_map<wstring, StackMethod*> StackMethod::virutal_cache;
map<wstring, wstring> StackProgram::properties_map;

#ifdef _SCRIPTER
StackProgram* Scripter::program;

/****************************
* Scripter class
****************************/
StackProgram* Scripter::GetProgram() {
  return program;
}
#endif

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
        CheckMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), depth);
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
#ifdef _DEBUG
    for (int i = 0; i < depth; i++) {
      wcout << L"\t";
    }
    wcout << L"\t----- SERIALIZING object: value=Nil -----" << endl;
#endif
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
      
    case OBJ_ARY_PARM: {
      long* array = (long*)(*mem);
      if(array) {
        SerializeByte(1);
        // mark data
        if(!WasSerialized((long*)(*mem))) {
          const long array_size = array[0];
#ifdef _DEBUG
          wcout << L"\t" << i << L": ----- serializing objeck array: mem_id=" << cur_id << L", size="
		<< array_size << L" byte(s) -----" << endl;
#endif
          // write metadata
          SerializeInt(array[0]);
          SerializeInt(array[1]);
          SerializeInt(array[2]);
          long* array_ptr = array + 3;

          // write values
          for (int i = 0; i < array_size; i++) {
            CheckObject((long*)(array_ptr[i]), true, depth + 1);
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
#ifdef _SCRIPTER
  cls = Scripter::GetProgram()->GetClass(obj_id);
#else
  cls = Loader::GetProgram()->GetClass(obj_id);
#endif
  if(cls) {
    INT_VALUE mem_id = DeserializeInt();
    if(mem_id < 0) {
      instance = MemoryManager::AllocateObject(cls->GetId(), op_stack, *stack_pos, false);
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
          wcout << L"--- deserialization: char array; value=" << out <<  ", size=" 
                << char_array_size << L" ---" << endl;
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

    case OBJ_ARY_PARM: {
      if(!DeserializeByte()) {
        instance[instance_pos++] = 0;
      }
      else {
        INT_VALUE mem_id = DeserializeInt();
        if (mem_id < 0) {
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
            if(!DeserializeByte()) {
              instance[instance_pos++] = 0;
            }
            else {
              ObjectDeserializer deserializer(buffer, buffer_offset, mem_cache,
					      buffer_array_size, op_stack, stack_pos);
              array_ptr[i] = (long)deserializer.DeserializeObject();
              buffer_offset = deserializer.GetOffset();
              mem_cache = deserializer.GetMemoryCache();
            }
          }
#ifdef _DEBUG
          wcout << L"--- deserialization: object array; value=" << array << ",  size="
		<< array_size << L" ---" << endl;
#endif
          // update cache
          mem_cache[-mem_id] = array;
          instance[instance_pos++] = (long)array;
        }
        else {
          map<INT_VALUE, long*>::iterator found = mem_cache.find(mem_id);
          if (found == mem_cache.end()) {
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
void APITools_MethodCall(size_t* op_stack, long *stack_pos, long *instance, int cls_id, int mthd_id) {
#ifdef _SCRIPTER
  StackClass* cls = Scripter::GetProgram()->GetClass(cls_id);
#else
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
#endif
  
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, (long*)stack_pos, 0, mthd, instance, false);
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

void APITools_MethodCall(size_t* op_stack, long *stack_pos, long *instance, 
                         const wchar_t* cls_id, const wchar_t* mthd_id) 
{
#ifdef _SCRIPTER
  StackClass* cls = Scripter::GetProgram()->GetClass(cls_id);
#else
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
#endif
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, (long*)stack_pos, 0, mthd, instance, false);
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

void APITools_MethodCallId(size_t* op_stack, long *stack_pos, long *instance, 
                           const int cls_id, const int mthd_id) 
{
#ifdef _SCRIPTER
  StackClass* cls = Scripter::GetProgram()->GetClass(cls_id);
#else
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
#endif
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, (long*)stack_pos, 0, mthd, instance, false);
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
void TrapProcessor::CreateNewObject(const wstring &cls_id, size_t* &op_stack, long* &stack_pos) {
  long* obj = MemoryManager::AllocateObject(cls_id.c_str(), op_stack, *stack_pos, false);
  if(obj) {
    // instance will be put on stack by method call
    const wstring mthd_name = cls_id + L":New:";
    APITools_MethodCall(op_stack, stack_pos, obj, cls_id.c_str(), mthd_name.c_str());
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

/********************************
 * Creates a container for a method
 ********************************/
long* TrapProcessor::CreateMethodObject(long* cls_obj, StackMethod* mthd, StackProgram* program, 
                                        size_t* &op_stack, long* &stack_pos) {
  long* mthd_obj = MemoryManager::AllocateObject(program->GetMethodObjectId(),
                                                 op_stack, *stack_pos,
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
                                                        op_stack, *stack_pos,
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
void TrapProcessor::CreateClassObject(StackClass* cls, long* cls_obj, size_t* &op_stack, 
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
                                        size_t* &op_stack, long* &stack_pos) {
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
                                                op_stack, *stack_pos,
                                                false);
  str_obj[0] = (long)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;
      
  return str_obj;
}

/********************************
 * Date/time calculations
 ********************************/
void TrapProcessor::ProcessTimerStart(size_t* &op_stack, long* &stack_pos)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  instance[0] = clock();
}

void TrapProcessor::ProcessTimerEnd(size_t* &op_stack, long* &stack_pos)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  instance[0] = clock() - (clock_t)instance[0];
}

void TrapProcessor::ProcessTimerElapsed(size_t* &op_stack, long* &stack_pos)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  PushFloat((double)instance[0] / (double)CLOCKS_PER_SEC, op_stack, stack_pos);
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
    instance[6] = curr_time->tm_isdst;         // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void TrapProcessor::ProcessSetTime1(size_t* &op_stack, long* &stack_pos)
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
    if(curr_time) {
      curr_time->tm_year = year - 1900;
      curr_time->tm_mon = month - 1;
      curr_time->tm_mday = day;
      curr_time->tm_hour = 0;
      curr_time->tm_min = 0;
      curr_time->tm_sec = 0;
      curr_time->tm_isdst = -1;
      mktime(curr_time);
    }

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst;         // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Sets a time instance
 ********************************/
void TrapProcessor::ProcessSetTime2(size_t* &op_stack, long* &stack_pos)
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
    curr_time->tm_isdst = -1;
    mktime(curr_time);

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst;         // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void TrapProcessor::ProcessSetTime3(size_t* &op_stack, long* &stack_pos)
{
}

void TrapProcessor::ProcessAddTime(TimeInterval t, size_t* &op_stack, long* &stack_pos)
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
    set_time.tm_isdst = instance[6];         // savings time

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
    if(curr_time) {
      instance[0] = curr_time->tm_mday;          // day
      instance[1] = curr_time->tm_mon + 1;       // month
      instance[2] = curr_time->tm_year + 1900;   // year
      instance[3] = curr_time->tm_hour;          // hours
      instance[4] = curr_time->tm_min;           // mins
      instance[5] = curr_time->tm_sec;           // secs
      instance[6] = curr_time->tm_isdst;         // savings time
      instance[7] = curr_time->tm_wday;          // day of week
    }
  }
}

/********************************
 * Get platform string
 ********************************/
void TrapProcessor::ProcessPlatform(StackProgram* program, size_t* &op_stack, long* &stack_pos) 
{
  const wstring value_str = BytesToUnicode(System::GetPlatform());
  long* str_obj = CreateStringObject(value_str, program, op_stack, stack_pos);
  PushInt((long)str_obj, op_stack, stack_pos);
}

/********************************
 * Get file owner string
 ********************************/
void TrapProcessor::ProcessFileOwner(const char* name, bool is_account,
				     StackProgram* program, size_t* &op_stack, long* &stack_pos) {
  const wstring value_str = File::FileOwner(name, is_account);
  if(value_str.size() > 0) {
    long* str_obj = CreateStringObject(value_str, program, op_stack, stack_pos);
    PushInt((long)str_obj, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

/********************************
 * Get version string
 ********************************/
void TrapProcessor::ProcessVersion(StackProgram* program, size_t* &op_stack, long* &stack_pos) 
{
  long* str_obj = CreateStringObject(VERSION_STRING, program, op_stack, stack_pos);
  PushInt((long)str_obj, op_stack, stack_pos);
}

//
// deserializes an array of objects
// 
inline long* TrapProcessor::DeserializeArray(ParamType type, long* inst, size_t* &op_stack, long* &stack_pos) {
  if(!DeserializeByte(inst)) {
    return NULL;
  }

  long* src_array = (long*)inst[0];
  long dest_pos = inst[1];

  if(dest_pos < src_array[0]) {
    const long dest_array_size = DeserializeInt(inst);
    const long dest_array_dim = DeserializeInt(inst);
    const long dest_array_dim_size = DeserializeInt(inst);

    long* dest_array;
    if(type == BYTE_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size + ((dest_array_dim + 2) * sizeof(long)),
                                                       BYTE_ARY_TYPE, op_stack, *stack_pos, false);
    }
    else if(type == CHAR_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size + ((dest_array_dim + 2) * sizeof(long)),
                                                       CHAR_ARY_TYPE, op_stack, *stack_pos, false);
    }
    else if(type == INT_ARY_PARM || type == OBJ_ARY_PARM) {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size + dest_array_dim + 2,
                                                       INT_TYPE, op_stack, *stack_pos, false);
    }
    else {
      dest_array = (long*)MemoryManager::AllocateArray(dest_array_size + dest_array_dim + 2,
                                                       FLOAT_TYPE, op_stack, *stack_pos, false);
    }

    // read array meta data
    dest_array[0] = dest_array_size;
    dest_array[1] = dest_array_dim;
    dest_array[2] = dest_array_dim_size;

    if(type == OBJ_ARY_PARM) {
      long* dest_array_ptr = dest_array + 3;
      for(int i = 0; i < dest_array_size; ++i) {
        if(!DeserializeByte(inst)) {
          dest_array_ptr[i] = 0;
        }
        else {
          const long dest_pos = inst[1];
          const long byte_array_dim_size = src_array[2];
          const char* byte_array_ptr = ((char*)(src_array + 3) + dest_pos);

          ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
          dest_array_ptr[i] = (long)deserializer.DeserializeObject();
          inst[1] = dest_pos + deserializer.GetOffset();
        }
      }
    }
    else {
      ReadSerializedBytes(dest_array, src_array, type, inst);
    }

    return dest_array;
  }

  return NULL;
}

//
// expand buffer
//
long* TrapProcessor::ExpandSerialBuffer(const long src_buffer_size, long* dest_buffer, 
                                        long* inst, size_t* &op_stack, long* &stack_pos) {
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
    long* byte_array = (long*)MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(long)),
                                                           BYTE_ARY_TYPE, op_stack, *stack_pos, false);
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
void TrapProcessor::SerializeObject(long* inst, StackFrame* frame, size_t* &op_stack, long* &stack_pos)
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
void TrapProcessor::DeserializeObject(long* inst, size_t* &op_stack, long* &stack_pos) {
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
                                size_t* &op_stack, long* &stack_pos, StackFrame* frame) {
  const long id = PopInt(op_stack, stack_pos);
  switch(id) {
  case LOAD_CLS_INST_ID:
    return LoadClsInstId(program, inst, op_stack, stack_pos, frame);

  case LOAD_NEW_OBJ_INST:
    return LoadNewObjInst(program, inst, op_stack, stack_pos, frame);

  case LOAD_CLS_BY_INST:
    return LoadClsByInst(program, inst, op_stack, stack_pos, frame);

  case BYTES_TO_UNICODE:
    return ConvertBytesToUnicode(program, inst, op_stack, stack_pos, frame);

  case UNICODE_TO_BYTES:
    return ConvertUnicodeToBytes(program, inst, op_stack, stack_pos, frame);

  case LOAD_MULTI_ARY_SIZE:
    return LoadMultiArySize(program, inst, op_stack, stack_pos, frame);

  case CPY_CHAR_STR_ARY:
    return CpyCharStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_CHAR_STR_ARYS:
    return CpyCharStrArys(program, inst, op_stack, stack_pos, frame);

  case CPY_INT_STR_ARY:
    return CpyIntStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_FLOAT_STR_ARY:
    return CpyFloatStrAry(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BOOL:
    return StdOutBool(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BYTE:
    return StdOutByte(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_CHAR:
    return StdOutChar(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_INT:
    return StdOutInt(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_FLOAT:
    return StdOutFloat(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_CHAR_ARY:
    return StdOutCharAry(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BYTE_ARY_LEN:
    return StdOutByteAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_CHAR_ARY_LEN:
    return StdOutCharAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_IN_STRING:
    return StdInString(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_BOOL:
    return StdErrBool(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_BYTE:
    return StdErrByte(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_CHAR:
    return StdErrChar(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_INT:
    return StdErrInt(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_FLOAT:
    return StdErrFloat(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_CHAR_ARY:
    return StdErrCharAry(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_BYTE_ARY:
    return StdErrByteAry(program, inst, op_stack, stack_pos, frame);

  case EXIT:
    return Exit(program, inst, op_stack, stack_pos, frame);

  case GMT_TIME:
    return GmtTime(program, inst, op_stack, stack_pos, frame);

  case SYS_TIME:
    return SysTime(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_SET_1:
    return DateTimeSet1(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_SET_2:
    return DateTimeSet2(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_DAYS:
    return DateTimeAddDays(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_HOURS:
    return DateTimeAddHours(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_MINS:
    return DateTimeAddMins(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_SECS:
    return DateTimeAddSecs(program, inst, op_stack, stack_pos, frame);

  case TIMER_START:
    return TimerStart(program, inst, op_stack, stack_pos, frame);

  case TIMER_END:
    return TimerEnd(program, inst, op_stack, stack_pos, frame);

  case TIMER_ELAPSED:
    return TimerElapsed(program, inst, op_stack, stack_pos, frame);

  case GET_PLTFRM:
    return GetPltfrm(program, inst, op_stack, stack_pos, frame);

  case GET_VERSION:
    return GetVersion(program, inst, op_stack, stack_pos, frame);

  case GET_SYS_PROP:
    return GetSysProp(program, inst, op_stack, stack_pos, frame);

  case SET_SYS_PROP:
    return SetSysProp(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_RESOLVE_NAME:
    return SockTcpResolveName(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_HOST_NAME:
    return SockTcpHostName(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_CONNECT:
    return SockTcpConnect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_BIND:
    return SockTcpBind(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_LISTEN:
    return SockTcpListen(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_ACCEPT:
    return SockTcpAccept(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_CLOSE:
    return SockTcpClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_STRING:
    return SockTcpOutString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_STRING:
    return SockTcpInString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_CONNECT:
    return SockTcpSslConnect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_CERT:
    return SockTcpSslCert(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_CLOSE:
    return SockTcpSslClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_STRING:
    return SockTcpSslOutString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_STRING:
    return SockTcpSslInString(program, inst, op_stack, stack_pos, frame);

  case SERL_CHAR:
    return SerlChar(program, inst, op_stack, stack_pos, frame);

  case SERL_INT:
    return SerlInt(program, inst, op_stack, stack_pos, frame);

  case SERL_FLOAT:
    return SerlFloat(program, inst, op_stack, stack_pos, frame);

  case SERL_OBJ_INST:
    return SerlObjInst(program, inst, op_stack, stack_pos, frame);

  case SERL_BYTE_ARY:
    return SerlByteAry(program, inst, op_stack, stack_pos, frame);

  case SERL_CHAR_ARY:
    return SerlCharAry(program, inst, op_stack, stack_pos, frame);

  case SERL_INT_ARY:
    return SerlIntAry(program, inst, op_stack, stack_pos, frame);

  case SERL_OBJ_ARY:
    return SerlObjAry(program, inst, op_stack, stack_pos, frame);

  case SERL_FLOAT_ARY:
    return SerlFloatAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_CHAR:
    return DeserlChar(program, inst, op_stack, stack_pos, frame);

  case DESERL_INT:
    return DeserlInt(program, inst, op_stack, stack_pos, frame);

  case DESERL_FLOAT:
    return DeserlFloat(program, inst, op_stack, stack_pos, frame);

  case DESERL_OBJ_INST:
    return DeserlObjInst(program, inst, op_stack, stack_pos, frame);

  case DESERL_BYTE_ARY:
    return DeserlByteAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_CHAR_ARY:
    return DeserlCharAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_INT_ARY:
    return DeserlIntAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_OBJ_ARY:
    return DeserlObjAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_FLOAT_ARY:
    return DeserlFloatAry(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_READ:
    return FileOpenRead(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_APPEND:
    return FileOpenAppend(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_WRITE:
    return FileOpenWrite(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_READ_WRITE:
    return FileOpenReadWrite(program, inst, op_stack, stack_pos, frame);

  case FILE_CLOSE:
    return FileClose(program, inst, op_stack, stack_pos, frame);

  case FILE_FLUSH:
    return FileFlush(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_STRING:
    return FileInString(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_STRING:
    return FileOutString(program, inst, op_stack, stack_pos, frame);

  case FILE_REWIND:
    return FileRewind(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IS_CONNECTED:
    return SockTcpIsConnected(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_BYTE:
    return SockTcpInByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_BYTE_ARY:
    return SockTcpInByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_CHAR_ARY:
    return SockTcpInCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_BYTE:
    return SockTcpOutByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_BYTE_ARY:
    return SockTcpOutByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_CHAR_ARY:
    return SockTcpOutCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_BYTE:
    return SockTcpSslInByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_BYTE_ARY:
    return SockTcpSslInByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_CHAR_ARY:
    return SockTcpSslInCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_BYTE:
    return SockTcpSslOutByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_BYTE_ARY:
    return SockTcpSslOutByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_CHAR_ARY:
    return SockTcpSslOutCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_BYTE:
    return FileInByte(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_CHAR_ARY:
    return FileInCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_BYTE_ARY:
    return FileInByteAry(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_BYTE:
    return FileOutByte(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_BYTE_ARY:
    return FileOutByteAry(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_CHAR_ARY:
    return FileOutCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_SEEK:
    return FileSeek(program, inst, op_stack, stack_pos, frame);

  case FILE_EOF:
    return FileEof(program, inst, op_stack, stack_pos, frame);

  case FILE_IS_OPEN:
    return FileIsOpen(program, inst, op_stack, stack_pos, frame);

  case FILE_EXISTS:
    return FileExists(program, inst, op_stack, stack_pos, frame);
      
  case FILE_CAN_WRITE_ONLY:
    return FileCanWriteOnly(program, inst, op_stack, stack_pos, frame);
      
  case FILE_CAN_READ_ONLY:
    return FileCanReadOnly(program, inst, op_stack, stack_pos, frame);

  case FILE_CAN_READ_WRITE:
    return FileCanReadWrite(program, inst, op_stack, stack_pos, frame);
    
  case FILE_SIZE:
    return FileSize(program, inst, op_stack, stack_pos, frame);

  case FILE_ACCOUNT_OWNER:
    return FileAccountOwner(program, inst, op_stack, stack_pos, frame);

  case FILE_GROUP_OWNER:
    return FileGroupOwner(program, inst, op_stack, stack_pos, frame);

  case FILE_DELETE:
    return FileDelete(program, inst, op_stack, stack_pos, frame);

  case FILE_RENAME:
    return FileRename(program, inst, op_stack, stack_pos, frame);

  case FILE_CREATE_TIME:
    return FileCreateTime(program, inst, op_stack, stack_pos, frame);

  case FILE_MODIFIED_TIME:
    return FileModifiedTime(program, inst, op_stack, stack_pos, frame);

  case FILE_ACCESSED_TIME:
    return FileAccessedTime(program, inst, op_stack, stack_pos, frame);

  case DIR_CREATE:
    return DirCreate(program, inst, op_stack, stack_pos, frame);

  case DIR_EXISTS:
    return DirExists(program, inst, op_stack, stack_pos, frame);

  case DIR_LIST:
    return DirList(program, inst, op_stack, stack_pos, frame);
  }

  return false;
}

bool TrapProcessor::LoadClsInstId(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* obj = (long*)PopInt(op_stack, stack_pos);
  PushInt(MemoryManager::GetObjectID(obj), op_stack, stack_pos);
  return true;
}

bool TrapProcessor::LoadNewObjInst(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (long*)array[0];
    const wchar_t* name = (wchar_t*)(array + 3);
#ifdef _DEBUG
    wcout << L"stack oper: LOAD_NEW_OBJ_INST; name='" << name << L"'" << endl;
#endif
    CreateNewObject(name, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::LoadClsByInst(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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
						op_stack, *stack_pos, false);
  cls_obj[0] = (long)CreateStringObject(cls->GetName(), program, op_stack, stack_pos);
  frame->mem[1] = (long)cls_obj;
  CreateClassObject(cls, cls_obj, op_stack, stack_pos, program);

  return true;
}

bool TrapProcessor::ConvertBytesToUnicode(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::ConvertUnicodeToBytes(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
    return false;
  }
  const string out = UnicodeToBytes((wchar_t*)(array + 3));

  // create byte array
  const long byte_array_size = out.size();
  const long byte_array_dim = 1;
  long* byte_array = (long*)MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(long)), 
                                                         BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy bytes
  char* byte_array_ptr = (char*)(byte_array + 3);
  strncpy(byte_array_ptr, out.c_str(), byte_array_size);

  // push result
  PushInt((long)byte_array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::LoadMultiArySize(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::CpyCharStrAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::CpyCharStrArys(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::CpyIntStrAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::CpyFloatStrAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::StdOutBool(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_OUT_BOOL" << endl;
#endif
  wcout << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");

  return true;
}

bool TrapProcessor::StdOutByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_OUT_BYTE" << endl;
#endif
  wcout << (unsigned char)PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdOutChar(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_OUT_CHAR" << endl;
#endif
  wcout << (wchar_t)PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdOutInt(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_OUT_INT" << endl;
#endif
  wcout << PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdOutFloat(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_OUT_FLOAT" << endl;
#endif
  wcout.precision(9);
  wcout << PopFloat(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdOutCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::StdOutByteAryLen(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  wcout << L"  STD_OUT_BYTE_ARY_LEN: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

  if(array && offset > -1 && offset + num < (long)array[0]) {
    const char* buffer = (char*)(array + 3);
    for(long i = 0; i < num; i++) {
      wcout << (char)buffer[i + offset];
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    wcout << L"Nil";
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdOutCharAryLen(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  wcout << L"  STD_OUT_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

  if(array && offset > -1 && offset + num < (long)array[0]) {
    const wchar_t* buffer = (wchar_t*)(array + 3);
    wcout.write(buffer + offset, num);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    wcout << L"Nil";
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdInString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    // read input
    const long max = array[2];
    wchar_t* buffer = new wchar_t[max + 1];
    wcin.getline(buffer, max);
    // copy to dest
    wchar_t* dest = (wchar_t*)(array + 3);
    wcsncpy(dest, buffer, max);
    // clean up
    delete[] buffer;
    buffer = NULL;
  }

  return true;
}

bool TrapProcessor::StdErrBool(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_ERR_BOOL" << endl;
#endif
  wcerr << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");

  return true;
}

bool TrapProcessor::StdErrByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_ERR_BYTE" << endl;
#endif
  wcerr << (unsigned char)PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrChar(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_ERR_CHAR" << endl;
#endif
  wcerr << (char)PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrInt(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_ERR_INT" << endl;
#endif
  wcerr << PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrFloat(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"  STD_ERR_FLOAT" << endl;
#endif
  wcerr.precision(9);
  wcerr << PopFloat(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::StdErrByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  wcout << L"  STD_ERR_CHAR_ARY: addr=" << array << L"(" << long(array) << L")" << endl;
#endif

  if(array && offset > -1 && offset + num < array[2]) {
    const unsigned char* buffer = (unsigned char*)(array + 3);
    for(long i = 0; i < num; i++) {
      wcerr << (char)buffer[i + offset];
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    wcerr << L"Nil";
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::Exit(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  exit(PopInt(op_stack, stack_pos));

  return true;
}

bool TrapProcessor::GmtTime(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessCurrentTime(frame, true);

  return true;
}

bool TrapProcessor::SysTime(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessCurrentTime(frame, false);

  return true;
}

bool TrapProcessor::DateTimeSet1(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessSetTime1(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeSet2(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessSetTime2(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddDays(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(DAY_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddHours(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(HOUR_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddMins(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(MIN_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddSecs(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(SEC_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::TimerStart(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessTimerStart(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::TimerEnd(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessTimerEnd(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::TimerElapsed(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessTimerElapsed(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::GetPltfrm(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessPlatform(program, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::GetVersion(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  ProcessVersion(program, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::GetSysProp(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::SetSysProp(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* value_array = (long*)PopInt(op_stack, stack_pos);
  long* key_array = (long*)PopInt(op_stack, stack_pos);

  if(key_array && value_array) {
    value_array = (long*)value_array[0];
    key_array = (long*)key_array[0];

    const wchar_t* key = (wchar_t*)(key_array + 3);
    const wchar_t* value = (wchar_t*)(value_array + 3);
    program->SetProperty(key, value);
  }

  return true;
}

bool TrapProcessor::SockTcpResolveName(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  array = (long*)array[0];
  if(array) {
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    vector<string> addrs = IPSocket::Resolve(name.c_str());

    // create 'System.String' object array
    const long str_obj_array_size = addrs.size();
    const long str_obj_array_dim = 1;
    long* str_obj_array = (long*)MemoryManager::AllocateArray(str_obj_array_size + str_obj_array_dim + 2,
                                                              INT_TYPE, op_stack, *stack_pos, false);
    str_obj_array[0] = str_obj_array_size;
    str_obj_array[1] = str_obj_array_dim;
    str_obj_array[2] = str_obj_array_size;
    long* str_obj_array_ptr = str_obj_array + 3;

    // create and assign 'System.String' instances to array
    for(size_t i = 0; i < addrs.size(); ++i) {
      const wstring waddr(addrs[i].begin(), addrs[i].end());
      str_obj_array_ptr[i] = (long)CreateStringObject(waddr, program, op_stack, stack_pos);
    }

    PushInt((long)str_obj_array, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpHostName(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    const long size = array[2];
    wchar_t* str = (wchar_t*)(array + 3);

    // get host name
    char buffer[SMALL_BUFFER_MAX + 1];
    if(!gethostname(buffer, SMALL_BUFFER_MAX)) {
      // copy name   
      long i = 0;
      for(; buffer[i] != L'\0' && i < size; ++i) {
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

  return true;
}

bool TrapProcessor::SockTcpConnect(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::SockTcpBind(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::SockTcpListen(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long backlog = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(instance && (long)instance[0] > -1) {
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

  return true;
}

bool TrapProcessor::SockTcpAccept(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
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
						   op_stack, *stack_pos, false);
    sock_obj[0] = client;
    sock_obj[1] = (long)CreateStringObject(wclient_address, program, op_stack, stack_pos);
    sock_obj[2] = client_port;

    PushInt((long)sock_obj, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpClose(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
#ifdef _DEBUG
    wcout << L"# socket close: addr=" << sock << L"(" << (long)sock << L") #" << endl;
#endif	
    instance[0] = 0;
    IPSocket::Close(sock);
  }

  return true;
}

bool TrapProcessor::SockTcpOutString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    const wchar_t* wdata = (wchar_t*)(array + 3);

#ifdef _DEBUG
    wcout << L"# socket write string: instance=" << instance << L"(" << (long)instance << L")"
	  << L"; array=" << array << L"(" << (long)array << L")" << L"; data=" << wdata << endl;
#endif	      
    if((long)sock > -1) {
      const string data = UnicodeToBytes(wdata);
      IPSocket::WriteBytes(data.c_str(), data.size(), sock);
    }
  }

  return true;
}

bool TrapProcessor::SockTcpInString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    char buffer[LARGE_BUFFER_MAX + 1];
    SOCKET sock = (SOCKET)instance[0];
    int status;

    if((long)sock > -1) {
      int index = 0;
      char value;
      bool end_line = false;
      do {
        value = IPSocket::ReadByte(sock, status);
        if(value != '\r' && value != '\n' && index < LARGE_BUFFER_MAX && status > 0) {
          buffer[index++] = value;
        }
        else {
          end_line = true;
        }
      } while(!end_line);
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

  return true;
}

bool TrapProcessor::SockTcpSslConnect(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long port = PopInt(op_stack, stack_pos);
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (long*)array[0];
    const wstring waddr((wchar_t*)(array + 3));
    const string addr(waddr.begin(), waddr.end());

    IPSecureSocket::Close((SSL_CTX*)instance[0], (BIO*)instance[1], (X509*)instance[2]);

    SSL_CTX* ctx; BIO* bio; X509* cert;
    bool is_open = IPSecureSocket::Open(addr.c_str(), port, ctx, bio, cert);
    instance[0] = (long)ctx;
    instance[1] = (long)bio;
    instance[2] = (long)cert;
    instance[3] = is_open;

#ifdef _DEBUG
    wcout << L"# socket connect: addr='" << waddr << L":" << port << L"'; instance="
	  << instance << L"(" << (long)instance << L")" << L"; addr=" << ctx << L"|" << bio << L"("
	  << (long)ctx << L"|" << (long)bio << L") #" << endl;
#endif
  }

  return true;
}

bool TrapProcessor::SockTcpSslCert(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  X509* cert = (X509*)instance[2];
  if(cert) {
    char buffer[LARGE_BUFFER_MAX + 1];
    X509_NAME_oneline(X509_get_subject_name(cert), buffer, LARGE_BUFFER_MAX);
    const wstring in = BytesToUnicode(buffer);
    PushInt((long)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslClose(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  SSL_CTX* ctx = (SSL_CTX*)instance[0];
  BIO* bio = (BIO*)instance[1];
  X509* cert = (X509*)instance[2];

#ifdef _DEBUG
  wcout << L"# socket close: addr=" << ctx << L"|" << bio << L"("
	<< (long)ctx << L"|" << (long)bio << L") #" << endl;
#endif      
  IPSecureSocket::Close(ctx, bio, cert);
  instance[0] = instance[1] = instance[2] = instance[3] = 0;

  return true;
}

bool TrapProcessor::SockTcpSslOutString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    const wstring data((wchar_t*)(array + 3));
    if(instance[3]) {
      const string out = UnicodeToBytes(data);
      IPSecureSocket::WriteBytes(out.c_str(), out.size(), ctx, bio);
    }
  }

  return true;
}

bool TrapProcessor::SockTcpSslInString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    char buffer[LARGE_BUFFER_MAX + 1];
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    int status;
    if(instance[3]) {
      int index = 0;
      char value;
      bool end_line = false;
      do {
        value = IPSecureSocket::ReadByte(ctx, bio, status);
        if(value != '\r' && value != '\n' && index < LARGE_BUFFER_MAX && status > 0) {
          buffer[index++] = value;
        }
        else {
          end_line = true;
        }
      } while(!end_line);
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

  return true;
}

bool TrapProcessor::SerlChar(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# serializing char #" << endl;
#endif
  SerializeInt(CHAR_PARM, inst, op_stack, stack_pos);
  SerializeChar((wchar_t)frame->mem[1], inst, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SerlInt(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# serializing int #" << endl;
#endif
  SerializeInt(INT_PARM, inst, op_stack, stack_pos);
  SerializeInt(frame->mem[1], inst, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SerlFloat(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# serializing float #" << endl;
#endif
  SerializeInt(FLOAT_PARM, inst, op_stack, stack_pos);
  FLOAT_VALUE value;
  memcpy(&value, &(frame->mem[1]), sizeof(value));
  SerializeFloat(value, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlObjInst(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeObject(inst, frame, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeInt(BYTE_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((long*)frame->mem[1], BYTE_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeInt(CHAR_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((long*)frame->mem[1], CHAR_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlIntAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeInt(INT_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((long*)frame->mem[1], INT_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlObjAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeInt(OBJ_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((long*)frame->mem[1], OBJ_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlFloatAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  SerializeInt(FLOAT_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((long*)frame->mem[1], FLOAT_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DeserlChar(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing char #" << endl;
#endif
  if(CHAR_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt(DeserializeChar(inst), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlInt(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing int #" << endl;
#endif
  if(INT_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt(DeserializeInt(inst), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlFloat(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing float #" << endl;
#endif
  if(FLOAT_PARM == (ParamType)DeserializeInt(inst)) {
    PushFloat(DeserializeFloat(inst), op_stack, stack_pos);
  }
  else {
    PushFloat(0.0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlObjInst(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  DeserializeObject(inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DeserlByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing byte array #" << endl;
#endif
  if(BYTE_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((long)DeserializeArray(BYTE_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing char array #" << endl;
#endif
  if(CHAR_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((long)DeserializeArray(CHAR_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlIntAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing int array #" << endl;
#endif
  if(INT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((long)DeserializeArray(INT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlObjAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing an object array #" << endl;
#endif
  if(OBJ_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((long)DeserializeArray(OBJ_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlFloatAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  wcout << L"# deserializing float array #" << endl;
#endif
  if(FLOAT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((long)DeserializeArray(FLOAT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOpenRead(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileOpenAppend(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (long*)array[0];
    const wstring name((wchar_t*)(array + 3));
    const string filename(name.begin(), name.end());
    FILE* file = File::FileOpen(filename.c_str(), "ab");
#ifdef _DEBUG
    wcout << L"# file open: name='" << name << L"'; instance=" << instance << L"("
	  << (long)instance << L")" << L"; addr=" << file << L"(" << (long)file
	  << L") #" << endl;
#endif
    instance[0] = (long)file;
  }

  return true;
}

bool TrapProcessor::FileOpenWrite(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileOpenReadWrite(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileClose(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
    wcout << L"# file close: addr=" << file << L"(" << (long)file << L") #" << endl;
#endif
    instance[0] = 0;
    fclose(file);
  }

  return true;
}

bool TrapProcessor::FileFlush(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
    wcout << L"# file close: addr=" << file << L"(" << (long)file << L") #" << endl;
#endif
    instance[0] = 0;
    fflush(file);
  }

  return true;
}

bool TrapProcessor::FileInString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    FILE* file = (FILE*)instance[0];
    char buffer[SMALL_BUFFER_MAX + 1];
    if(file && fgets(buffer, SMALL_BUFFER_MAX, file)) {
      long end_index = strlen(buffer) - 1;
      if(end_index > -1) {
        if(buffer[end_index] == '\n' || buffer[end_index] == '\r') {
          buffer[end_index] = '\0';
        }

        if(--end_index > -1 && buffer[end_index] == '\r') {
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

  return true;
}

bool TrapProcessor::FileOutString(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    FILE* file = (FILE*)instance[0];
    const wchar_t* data = (wchar_t*)(array + 3);
    if(file) {
      fputs(UnicodeToBytes(data).c_str(), file);
    }
  }

  return true;
}

bool TrapProcessor::FileRewind(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    rewind(file);
  }

  return true;
}

bool TrapProcessor::SockTcpIsConnected(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    int status;
    PushInt(IPSocket::ReadByte(sock, status), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset > -1 && offset + num <= array[0]) {
    SOCKET sock = (SOCKET)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(IPSocket::ReadBytes(buffer + offset, num, sock), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset > -1 && offset + num <= array[0]) {
    SOCKET sock = (SOCKET)instance[0];
    wchar_t* buffer = (wchar_t*)(array + 3);
    // allocate temporary buffer
    char* byte_buffer = new char[array[0] + 1];
    int read = IPSocket::ReadBytes(byte_buffer + offset, num, sock);
    if(read > -1) {
      byte_buffer[read] = '\0';
      wstring in = BytesToUnicode(byte_buffer);
      wcsncpy(buffer, in.c_str(), in.size());
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = NULL;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long value = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    IPSocket::WriteByte((char)value, sock);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset > -1 && offset + num <= array[0]) {
    SOCKET sock = (SOCKET)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(IPSocket::WriteBytes(buffer + offset, num, sock), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset > -1 && offset + num <= array[0]) {
    SOCKET sock = (SOCKET)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(IPSocket::WriteBytes(buffer_out.c_str(), buffer_out.size(), sock), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslInByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::SockTcpSslInByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[2] && offset > -1 && offset + num <= array[0]) {
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    char* buffer = (char*)(array + 3);
    PushInt(IPSecureSocket::ReadBytes(buffer + offset, num, ctx, bio), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslInCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[2] && offset > -1 && offset + num <= array[0]) {
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    wchar_t* buffer = (wchar_t*)(array + 3);
    char* byte_buffer = new char[array[0] + 1];
    int read = IPSecureSocket::ReadBytes(byte_buffer + offset, num, ctx, bio);
    if(read > -1) {
      byte_buffer[read] = '\0';
      wstring in = BytesToUnicode(byte_buffer);
      wcsncpy(buffer, in.c_str(), in.size());
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = NULL;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslOutByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::SockTcpSslOutByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[2] && offset > -1 && offset + num <= array[0]) {
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    char* buffer = (char*)(array + 3);
    PushInt(IPSecureSocket::WriteBytes(buffer + offset, num, ctx, bio), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslOutCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[2] && offset > -1 && offset + num <= array[0]) {
    SSL_CTX* ctx = (SSL_CTX*)instance[0];
    BIO* bio = (BIO*)instance[1];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(IPSecureSocket::WriteBytes(buffer_out.c_str(), buffer_out.size(), ctx, bio), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileInByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileInCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[0]) {
    FILE* file = (FILE*)instance[0];
    wchar_t* out = (wchar_t*)(array + 3);

    // read from file
    char* byte_buffer = new char[num + 1];
    const size_t max = fread(byte_buffer + offset, 1, num, file);
    byte_buffer[max] = '\0';
    const wstring in(BytesToUnicode(byte_buffer));

    // copy
    wcsncpy(out, in.c_str(), array[2]);

    // clean up
    delete[] byte_buffer;
    byte_buffer = NULL;

    PushInt(max, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileInByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[0]) {
    FILE* file = (FILE*)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(fread(buffer + offset, 1, num, file), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOutByte(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileOutByteAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[0]) {
    FILE* file = (FILE*)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(fwrite(buffer + offset, 1, num, file), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOutCharAry(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* array = (long*)PopInt(op_stack, stack_pos);
  const long num = PopInt(op_stack, stack_pos);
  const long offset = PopInt(op_stack, stack_pos);
  const long* instance = (long*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset > -1 && offset + num <= array[0]) {
    FILE* file = (FILE*)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(fwrite(buffer_out.c_str(), 1, buffer_out.size(), file), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileSeek(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileEof(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    PushInt(feof(file) != 0, op_stack, stack_pos);
  }
  else {
    PushInt(1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileIsOpen(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  const long* instance = (long*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileCanWriteOnly(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (long*)array[0];
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    PushInt(File::FileWriteOnly(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  
  return true;
}

bool TrapProcessor::FileCanReadOnly(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (long*)array[0];
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    PushInt(File::FileReadOnly(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  
  return true;
}

bool TrapProcessor::FileCanReadWrite(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if (array) {
    array = (long*)array[0];
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    PushInt(File::FileReadWrite(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileExists(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileSize(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileAccountOwner(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (long*)array[0];
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    ProcessFileOwner(name.c_str(), true, program, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileGroupOwner(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (long*)array[0];
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    ProcessFileOwner(name.c_str(), false, program, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileDelete(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileRename(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileCreateTime(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileModifiedTime(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::FileAccessedTime(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::DirCreate(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::DirExists(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
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

  return true;
}

bool TrapProcessor::DirList(StackProgram* program, long* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame)
{
  long* array = (long*)PopInt(op_stack, stack_pos);
  array = (long*)array[0];
  if(array) {
    const wstring wname((wchar_t*)(array + 3));
    const string name(wname.begin(), wname.end());
    vector<string> files = File::ListDir(name.c_str());

    // create 'System.String' object array
    const long str_obj_array_size = files.size();
    const long str_obj_array_dim = 1;
    long* str_obj_array = (long*)MemoryManager::AllocateArray(str_obj_array_size + str_obj_array_dim + 2,
                                                              INT_TYPE, op_stack, *stack_pos, false);
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

  return true;
}

#endif
