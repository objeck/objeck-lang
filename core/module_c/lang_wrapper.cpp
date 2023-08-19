#include "lang_capi.h"

	CObjL* CObjL_new(const char* source, const char* lib_uses, const char* cmd_args) {
		auto _source = BytesToUnicode(string(source));
		auto _lib_uses = BytesToUnicode(string(lib_uses));
		auto _cmd_args = BytesToUnicode(string(cmd_args));
		auto ObjL = new ObjeckLang(_source, _lib_uses, _cmd_args);
		return reinterpret_cast<CObjL*>(ObjL);
	}
	void CObjL_destroy(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		delete ObjL;
		_CObjL = nullptr;
	}
	bool CObjL_compile(CObjL* _CObjL, const char* file, const char* opt) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		auto _file = BytesToUnicode(string(file));
		auto _opt = BytesToUnicode(string(opt));
		return ObjL->Compile(_file, _opt);
	}
	void CObjL_getErrors(CObjL* _CObjL, const char** errors) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		auto v = ObjL->GetErrors();
		for(size_t i = 0; i < v.size(); i++) {
			errors[i] = UnicodeToBytes(v[i]).c_str();
		}
	}
#ifdef _MODULE_STDIO
	const char* CObjL_execute(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		auto result = UnicodeToBytes(ObjL->Execute());
		return result.c_str();
	}
#else
	void CObjL_execute(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute();
	}
#endif
