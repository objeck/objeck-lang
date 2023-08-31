#include "lang_wrapper.h"

	CObjL* CObjL_new(const char* lib_uses) {
		auto _lib_uses = BytesToUnicode(std::string(lib_uses));
		auto ObjL = new ObjeckLang(_lib_uses);
		return reinterpret_cast<CObjL*>(ObjL);
	}
	void CObjL_destroy(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		delete ObjL;
		_CObjL = nullptr;
	}
	bool CObjL_compile(CObjL* _CObjL, const char* file, const char* source, const char* opt_level) {
		auto _file = BytesToUnicode(std::string(file));
		auto _source = BytesToUnicode(std::string(source));
		auto _opt_level = BytesToUnicode(std::string(opt_level));
		std::vector<std::pair<std::wstring, std::wstring>> file_source;
		file_source.push_back(std::make_pair(_file, _source));
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Compile(file_source, _opt_level);
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
		return UnicodeToBytes(ObjL->Execute()).c_str();
	}
	const char* CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args) {
		auto _cmd_args = BytesToUnicode(std::string(cmd_args));
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return UnicodeToBytes(ObjL->Execute(_cmd_args)).c_str();
	}
#else
	void CObjL_execute(CObjL* _CObjL) {
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute();
	}
	void CObjL_execute_with_args(CObjL* _CObjL, const char* cmd_args) {
		auto _cmd_args = BytesToUnicode(std::string(cmd_args));
		auto ObjL = reinterpret_cast<ObjeckLang*>(_CObjL);
		return ObjL->Execute(_cmd_args);
	}
#endif
