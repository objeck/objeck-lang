#include "lang_wrapper_w.h"

	CObjL* CObjL_new(const wchar_t* lib_uses) {
		auto _lib_uses = std::wstring(lib_uses);
		auto ObjL = new ObjeckLang(_lib_uses);
		return reinterpret_cast<CObjL*>(ObjL);
	}
	void CObjL_destroy(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		delete ObjL;
		_CObjL = nullptr;
	}
	bool CObjL_compile(CObjL* _CObjL, const wchar_t* file, const wchar_t* source, const wchar_t* opt_level) {
		auto _file = std::wstring(file);
		auto _source = std::wstring(source);
		auto _opt_level = std::wstring(opt_level);
		std::vector<std::pair<std::wstring, std::wstring>> file_source;
		file_source.push_back(std::make_pair(_file, _source));
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Compile(file_source, _opt_level);
	}
	void CObjL_getErrors(CObjL* _CObjL, const wchar_t** errors) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		auto v = ObjL->GetErrors();
		for(size_t i = 0; i < v.size(); i++) {
			errors[i] = v[i].c_str();
		}
	}
#ifdef _MODULE_STDIO
	const wchar_t* CObjL_execute(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute().c_str();
	}
	const wchar_t* CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args) {
		auto _cmd_args = std::wstring(cmd_args);
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute(_cmd_args).c_str();
	}
#else
	void CObjL_execute(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute();
	}
	void CObjL_execute_with_args(CObjL* _CObjL, const wchar_t* cmd_args) {
		auto _cmd_args = std::wstring(cmd_args);
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute(_cmd_args);
	}
#endif
