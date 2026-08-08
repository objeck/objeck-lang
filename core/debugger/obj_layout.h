/***************************************************************************
 * Object and collection layout, shared by the debugger front ends.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __OBJ_LAYOUT_H__
#define __OBJ_LAYOUT_H__

#include "debugger.h"
#include <string>
#include <sstream>

// Reading an object means knowing where a class keeps its fields and where a
// collection keeps its contents. Both debugger front ends need that, and when
// each carried its own copy they drifted: the CLI reported a Vector's backing
// array instead of its size, and the DAP formatter read a String's Char[]
// capacity instead of its length. This header is the single owner of that
// knowledge.
//
// Everything here is a pure function of the object graph -- no adapter state --
// so the unit is header-only and no build file has to learn about it.

namespace Runtime {

  enum VarHandleKind {
    VAR_OBJECT = 0,
    VAR_ARRAY,
    VAR_VECTOR,
    VAR_MAP,
    VAR_HASH
  };

  // declared up front because several of these call one another
  inline bool FieldIndex(StackClass* klass, const std::wstring& short_name, int fallback_index, int& out_index);
  inline bool FindInstanceField(StackClass* klass, const std::wstring& short_name, int& out_index);
  inline std::wstring ClassLeafName(StackClass* klass);
  inline int CollectionKind(StackClass* klass);
  inline bool CollectionSize(StackClass* klass, size_t* obj, long& out_size);
  inline std::string CollectionSummary(StackClass* klass, size_t* obj);
  inline bool IsLeafObject(StackClass* klass);
  inline std::string DescribeObject(size_t* obj, StackClass* klass);
  inline std::string FormatSlot(ParamType type, size_t* mem, int index);
  inline bool ArrayBody(size_t* array, long& out_count, size_t*& out_data);
  inline std::string ElementName(long index);
  inline std::string FormatVariableValue(StackDclr& dclr, StackFrame* frame, int var_index);
  inline std::string FormatVariableValue(StackDclr& dclr, size_t* mem, int var_index);

  // Locates an instance field by its short name (declarations are stored
  // fully qualified, e.g. "Collection.Vector:@values") and reports the memory
  // slot it occupies. Looking fields up by name rather than by a hard-coded
  // slot keeps this working if a library class gains or reorders fields.
  // Library classes are compiled without debug symbols, so their declaration
  // names are blank and the lookup below cannot find them. For the handful of
  // collection internals we walk, fall back to the slot the field occupies in
  // the library source. The index is validated against the declaration count so
  // a layout change degrades to generic expansion instead of reading garbage.
  inline bool FieldIndex(StackClass* klass, const std::wstring& short_name, int fallback_index, int& out_index)
  {
    if(FindInstanceField(klass, short_name, out_index)) {
      return true;
    }

    if(klass && fallback_index >= 0 && fallback_index < (int)klass->GetNumberInstanceDeclarations()) {
      out_index = fallback_index;
      return true;
    }

    out_index = 0;
    return false;
  }

  inline bool FindInstanceField(StackClass* klass, const std::wstring& short_name, int& out_index)
  {
    out_index = 0;

    if(!klass || short_name.empty()) {
      return false;
    }

    StackDclr** dclrs = klass->GetInstanceDeclarations();
    const int dclrs_num = (int)klass->GetNumberInstanceDeclarations();
    if(!dclrs) {
      return false;
    }

    int mem_index = 0;
    for(int i = 0; i < dclrs_num; ++i) {
      StackDclr* dclr = dclrs[i];
      if(!dclr) {
        continue;
      }

      // Compare against the trailing name without copying it.
      const std::wstring& full_name = dclr->name;
      const size_t name_index = full_name.find_last_of(L':');
      const size_t leaf_pos = (name_index == std::wstring::npos) ? 0 : name_index + 1;

      if(full_name.compare(leaf_pos, std::wstring::npos, short_name) == 0) {
        out_index = mem_index;
        return true;
      }

      mem_index++;
      if(dclr->type == FUNC_PARM) {
        mem_index++;
      }
    }

    return false;
  }

  // Strips the namespace and any generic arguments: "Collection.Vector<...>"
  // becomes "Vector".
  inline std::wstring ClassLeafName(StackClass* klass)
  {
    if(!klass) {
      return L"";
    }

    const std::wstring& name = klass->GetName();
    const std::wstring base = name.substr(0, name.find(L'<'));
    const size_t dot_index = base.find_last_of(L'.');

    return (dot_index == std::wstring::npos) ? base : base.substr(dot_index + 1);
  }

  // Recognizes the standard collections so they can be presented as their
  // contents rather than their internals. Both the class name and the expected
  // fields must match, so a user class called "Map" is not misread.
  inline int CollectionKind(StackClass* klass)
  {
    if(!klass) {
      return VAR_OBJECT;
    }

    const std::wstring leaf = ClassLeafName(klass);

    // Library classes carry no declaration names, so the class name plus a
    // plausible field count is all there is to match on.
    const long fields = klass->GetNumberInstanceDeclarations();

    if((leaf == L"Vector" || leaf == L"CompareVector") && fields >= 2) {
      return VAR_VECTOR;
    }
    if(leaf == L"Map" && fields >= 3) {
      return VAR_MAP;
    }
    if(leaf == L"Hash" && fields >= 2) {
      return VAR_HASH;
    }

    return VAR_OBJECT;
  }

  // Element count of a recognized collection. @size sits at slot 1 in Vector and
  // Hash, slot 2 in Map -- the one place that mapping is written down.
  inline bool CollectionSize(StackClass* klass, size_t* obj, long& out_size)
  {
    out_size = 0;

    const int kind = CollectionKind(klass);
    if(kind == VAR_OBJECT || !obj) {
      return false;
    }

    int size_index;
    if(!FieldIndex(klass, L"@size", kind == VAR_MAP ? 2 : 1, size_index)) {
      return false;
    }

    out_size = (long)obj[size_index];
    return true;
  }

  // "Vector(size=3)" instead of "Collection.Vector@0x1f4a2c0". Empty when the
  // class is not a recognized collection.
  inline std::string CollectionSummary(StackClass* klass, size_t* obj)
  {
    long size;
    if(!CollectionSize(klass, obj, size)) {
      return "";
    }

    std::ostringstream oss;
    oss << UnicodeToBytes(ClassLeafName(klass)) << "(size=" << size << ')';
    return oss.str();
  }

  // Classes whose value is fully shown by DescribeObject, so drilling in would
  // only expose backing storage.
  inline bool IsLeafObject(StackClass* klass)
  {
    if(!klass) {
      return false;
    }

    const std::wstring& name = klass->GetName();
    return name == L"System.String" || name == L"System.IntRef" || name == L"System.BoolRef" ||
           name == L"System.CharRef" || name == L"System.ByteRef" || name == L"System.FloatRef";
  }

  // Renders an object as something a reader can use: the text of a string, the
  // contents of a boxed scalar, an element count for a collection. Falls back to
  // class@address for everything else.
  inline std::string DescribeObject(size_t* obj, StackClass* klass)
  {
    if(!obj || !klass) {
      return "";
    }

    const std::wstring& class_name = klass->GetName();

    // A String keeps its Char[] in the first slot, with the characters packed
    // from [3]. That array is a capacity buffer -- String allocates 8 cells up
    // front and grows -- so its dimension size at [2] counts allocated cells,
    // not characters. The text length lives on the String itself, in @pos.
    // Reading the array's size instead yields the text followed by the unused
    // cells as NULs, which is invisible wherever the value is printed as a
    // NUL-terminated string but not where it is read with an explicit length.
    if(class_name == L"System.String") {
      size_t* char_array = (size_t*)obj[0];
      if(!char_array) {
        return "\"\"";
      }

      const size_t capacity = char_array[2];
      size_t len = obj[2];
      if(len > capacity) {
        len = capacity;
      }
      const wchar_t* chars = (const wchar_t*)(char_array + 3);
      std::wstring text(chars, len < 256 ? len : 256);

      std::ostringstream oss;
      oss << '"' << UnicodeToBytes(text) << '"';
      if(len > 256) {
        oss << "...";
      }
      return oss.str();
    }

    // Boxed scalars keep their payload in the first slot.
    if(class_name == L"System.IntRef") {
      std::ostringstream oss;
      oss << (long)obj[0];
      return oss.str();
    }
    if(class_name == L"System.BoolRef") {
      return obj[0] ? "true" : "false";
    }
    if(class_name == L"System.CharRef") {
      std::ostringstream oss;
      oss << '\'' << (char)(wchar_t)obj[0] << '\'';
      return oss.str();
    }
    if(class_name == L"System.ByteRef") {
      std::ostringstream oss;
      oss << (int)(unsigned char)obj[0];
      return oss.str();
    }
    if(class_name == L"System.FloatRef") {
      double value;
      memcpy(&value, &obj[0], sizeof(double));
      std::ostringstream oss;
      oss << value;
      return oss.str();
    }

    return CollectionSummary(klass, obj);
  }

  // Formats a raw slot of a known type. FormatVariableValue is declaration-driven,
  // so the synthetic declaration lives here rather than at each call site.
  inline std::string FormatSlot(ParamType type, size_t* mem, int index)
  {
    StackDclr dclr;
    dclr.name = L"";
    dclr.type = type;
    dclr.id = 0;

    return FormatVariableValue(dclr, mem, index);
  }

  // Decodes an array header: [0] is the element count, [1] the dimension count,
  // then one size per dimension, then the elements.
  inline bool ArrayBody(size_t* array, long& out_count, size_t*& out_data)
  {
    out_count = 0;
    out_data = nullptr;

    if(!array) {
      return false;
    }

    const long size = (long)array[0];
    const long dim = (long)array[1];
    if(size < 0 || dim < 1 || dim > 8) {
      return false;
    }

    out_count = size;
    out_data = array + 2 + dim;

    return true;
  }

  // "[7]" -- building an ostringstream per element was the bulk of the cost of
  // rendering a large array.
  inline std::string ElementName(long index)
  {
    return "[" + std::to_string(index) + "]";
  }

  inline std::string FormatVariableValue(StackDclr& dclr, StackFrame* frame, int var_index)
  {
    if(!frame || !frame->mem || var_index < 0) {
      return "<unavailable>";
    }

    // Bounds: mem_size is a slot count (NewMemory allocates mem_size + 2 size_t
    // slots; +2 covers the @self slot and the and/or temporary).
    long mem_slots = frame->method->GetMemorySize() + 2;
    if(var_index >= mem_slots) {
      return "<out of scope>";
    }

    return FormatVariableValue(dclr, frame->mem, var_index);
  }

  inline std::string FormatVariableValue(StackDclr& dclr, size_t* mem, int var_index)
  {
    if(!mem || var_index < 0) {
      return "<unavailable>";
    }

    size_t value = mem[var_index];

    switch(dclr.type) {
      case CHAR_PARM:
        return std::string(1, (char)(wchar_t)value);

      case INT_PARM: {
        std::ostringstream oss;
        oss << (long)value;
        return oss.str();
      }

      case FLOAT_PARM: {
        double dval;
        memcpy(&dval, &mem[var_index], sizeof(double));
        std::ostringstream oss;
        oss << dval;
        return oss.str();
      }

      case CHAR_ARY_PARM: {
        if(value == 0) {
          return "Nil";
        }
        size_t* array = (size_t*)value;
        const size_t len = array[2];
        const wchar_t* chars = (const wchar_t*)(array + 3);
        if(len > 0) {
          const size_t preview_max = 48;
          std::wstring wstr(chars, std::min(len, preview_max));
          std::string result = "\"" + UnicodeToBytes(wstr) + "\"";
          if(len > preview_max) {
            result += "...";
          }
          return result;
        }
        return "\"\"";
      }

      case BYTE_ARY_PARM:
      case INT_ARY_PARM:
      case FLOAT_ARY_PARM:
      case OBJ_ARY_PARM: {
        if(value == 0) {
          return "Nil";
        }
        size_t* array = (size_t*)value;
        std::ostringstream oss;
        oss << "[size=" << array[0] << "]";
        return oss.str();
      }

      case OBJ_PARM: {
        if(value == 0) {
          return "Nil";
        }
        StackClass* klass = MemoryManager::GetClass((size_t*)value);
        std::ostringstream oss;
        if(klass) {
          // Strings, boxed scalars and collections describe themselves; an
          // address is what a reader actually wants least.
          const std::string described = DescribeObject((size_t*)value, klass);
          if(!described.empty()) {
            return described;
          }
          oss << UnicodeToBytes(klass->GetName()) << "@0x" << std::hex << value;
        }
        else {
          oss << "object@0x" << std::hex << value;
        }
        return oss.str();
      }

      case FUNC_PARM:
        return "<function>";

      default:
        return "<unknown>";
    }
  }
}

#endif
