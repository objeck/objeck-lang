/**************************************************************************
 * Language scanner.
 *
 * Copyright (c) 2025, Randy Hollines
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

#include "scanner.h"
#include <random>
#include <stdexcept>

#define EOB L'\0'

/****************************
 * Scanner constructor
 ****************************/
Scanner::Scanner(std::wstring f, bool a, const std::wstring c)
{
  // copy file name
  alt_syntax = a;
  cur_char = L'\0';

  // create tokens
  for(int i = 0; i < LOOK_AHEAD; ++i) {
    tokens[i] = new Token;
  }
  
  // load identifiers into map
  LoadKeywords();
  
  // read file into memory
  if(!c.empty()) {
    filename = f;
    buffer_pos = 0;
    is_first_token = true;
    buffer_size = c.size() + 1;
    buffer = new wchar_t[buffer_size];
#ifdef _WIN32
    wcscpy_s(buffer, buffer_size, c.c_str());
#else
    wcscpy(buffer, c.c_str());
#endif
    
#ifdef _DEBUG
    GetLogger() << L"---------- Source (inline) ---------" << std::endl;
    GetLogger() << buffer << std::endl;
#endif
  }
  else {
    filename = f;
    ReadFile();
  }
  // set line number to 1
  line_nbr = 1;
  line_pos = 1;
}

/****************************
 * Scanner destructor
 ****************************/
Scanner::~Scanner()
{
  // delete buffer
  if(buffer) {
    delete[] buffer;
    buffer = nullptr;
  }
  // delete token array
  for(int i = 0; i < LOOK_AHEAD; ++i) {
    delete tokens[i];
    tokens[i] = nullptr;
  }
}

/****************************
 * Loads language keywords
 ****************************/
void Scanner::LoadKeywords()
{
  ident_map[L"and"] = TOKEN_AND_ID;
  ident_map[L"not"] = TOKEN_NOT_ID;
  ident_map[L"or"] = TOKEN_OR_ID;
  ident_map[L"xor"] = TOKEN_XOR_ID;
  ident_map[L"virtual"] = TOKEN_VIRTUAL_ID;
  ident_map[L"if"] = TOKEN_IF_ID;
  ident_map[L"else"] = TOKEN_ELSE_ID;
  ident_map[L"do"] = TOKEN_DO_ID;
  ident_map[L"while"] = TOKEN_WHILE_ID;
  ident_map[L"break"] = TOKEN_BREAK_ID;
  ident_map[L"continue"] = TOKEN_CONTINUE_ID;
  ident_map[L"use"] = TOKEN_USE_ID;
  ident_map[L"bundle"] = TOKEN_BUNDLE_ID;
  ident_map[L"native"] = TOKEN_NATIVE_ID;
  ident_map[L"static"] = TOKEN_STATIC_ID;
  ident_map[L"public"] = TOKEN_PUBLIC_ID;
  ident_map[L"private"] = TOKEN_PRIVATE_ID;
  ident_map[L"class"] = TOKEN_CLASS_ID;
  ident_map[L"interface"] = TOKEN_INTERFACE_ID;
  ident_map[L"alias"] = TOKEN_ALIAS_ID;
  ident_map[L"implements"] = TOKEN_IMPLEMENTS_ID;
  ident_map[L"function"] = TOKEN_FUNCTION_ID;
  ident_map[L"method"] = TOKEN_METHOD_ID;
  ident_map[L"select"] = TOKEN_SELECT_ID;
  ident_map[L"other"] = TOKEN_OTHER_ID;
  ident_map[L"otherwise"] = TOKEN_OTHER_ID;
  ident_map[L"enum"] = TOKEN_ENUM_ID;
  ident_map[L"consts"] = TOKEN_CONSTS_ID;
  ident_map[L"for"] = TOKEN_FOR_ID;
  ident_map[L"each"] = TOKEN_EACH_ID;
  ident_map[L"in"] = TOKEN_IN_ID;
  ident_map[L"reverse"] = TOKEN_REVERSE_ID;
  ident_map[L"label"] = TOKEN_LABEL_ID;
  ident_map[L"return"] = TOKEN_RETURN_ID;
  ident_map[L"leaving"] = TOKEN_LEAVING_ID;
  ident_map[L"Byte"] = TOKEN_BYTE_ID;
  ident_map[L"Int"] = TOKEN_INT_ID;
  ident_map[L"Parent"] = TOKEN_PARENT_ID;
  ident_map[L"from"] = TOKEN_FROM_ID;
  ident_map[L"Float"] = TOKEN_FLOAT_ID;
  ident_map[L"Char"] = TOKEN_CHAR_ID;
  ident_map[L"Bool"] = TOKEN_BOOLEAN_ID;
  ident_map[L"true"] = TOKEN_TRUE_ID;
  ident_map[L"false"] = TOKEN_FALSE_ID;
  ident_map[L"New"] = TOKEN_NEW_ID;
  ident_map[L"Nil"] = TOKEN_NIL_ID;
  ident_map[L"As"] = TOKEN_AS_ID;
  ident_map[L"TypeOf"] = TOKEN_TYPE_OF_ID;
  ident_map[L"critical"] = TOKEN_CRITICAL_ID;
#ifdef _SYSTEM
  ident_map[L"S2I"] = S2I;
  ident_map[L"S2F"] = S2F;
  ident_map[L"I2S"] = I2S;
  ident_map[L"F2S"] = F2S;
  ident_map[L"LOAD_ARY_SIZE"] = LOAD_ARY_SIZE;
  ident_map[L"CPY_BYTE_ARY"] = CPY_BYTE_ARY;
  ident_map[L"CPY_CHAR_ARY"] = CPY_CHAR_ARY;
  ident_map[L"CPY_INT_ARY"] = CPY_INT_ARY;
  ident_map[L"CPY_FLOAT_ARY"] = CPY_FLOAT_ARY;
  ident_map[L"ZERO_BYTE_ARY"] = ZERO_BYTE_ARY;
  ident_map[L"ZERO_CHAR_ARY"] = ZERO_CHAR_ARY;
  ident_map[L"ZERO_INT_ARY"] = ZERO_INT_ARY;
  ident_map[L"ZERO_FLOAT_ARY"] = ZERO_FLOAT_ARY;
  ident_map[L"FLOR_FLOAT"] = FLOR_FLOAT;
  ident_map[L"CEIL_FLOAT"] = CEIL_FLOAT;
  ident_map[L"TRUNC_FLOAT"] = TRUNC_FLOAT;
  ident_map[L"SIN_FLOAT"] = SIN_FLOAT;
  ident_map[L"COS_FLOAT"] = COS_FLOAT;
  ident_map[L"TAN_FLOAT"] = TAN_FLOAT;
  ident_map[L"ASIN_FLOAT"] = ASIN_FLOAT;
  ident_map[L"ACOS_FLOAT"] = ACOS_FLOAT;
  ident_map[L"ATAN_FLOAT"] = ATAN_FLOAT;
  ident_map[L"LOG2_FLOAT"] = LOG2_FLOAT;
  ident_map[L"CBRT_FLOAT"] = CBRT_FLOAT;
  ident_map[L"ATAN2_FLOAT"] = ATAN2_FLOAT;
  ident_map[L"ACOSH_FLOAT"] = ACOSH_FLOAT;
  ident_map[L"ASINH_FLOAT"] = ASINH_FLOAT;
  ident_map[L"ATANH_FLOAT"] = ATANH_FLOAT;
  ident_map[L"COSH_FLOAT"] = COSH_FLOAT;
  ident_map[L"SINH_FLOAT"] = SINH_FLOAT;
  ident_map[L"TANH_FLOAT"] = TANH_FLOAT;
  ident_map[L"MOD_FLOAT"] = MOD_FLOAT;
  ident_map[L"LOG_FLOAT"] = LOG_FLOAT;
  ident_map[L"ROUND_FLOAT"] = ROUND_FLOAT;
  ident_map[L"EXP_FLOAT"] = EXP_FLOAT;
  ident_map[L"LOG10_FLOAT"] = LOG10_FLOAT;
  ident_map[L"POW_FLOAT"] = POW_FLOAT;
  ident_map[L"SQRT_FLOAT"] = SQRT_FLOAT;
  ident_map[L"GAMMA_FLOAT"] = GAMMA_FLOAT;
  ident_map[L"RAND_FLOAT"] = RAND_FLOAT;
  ident_map[L"NAN_INT"] = NAN_INT;
  ident_map[L"INF_INT"] = INF_INT;
  ident_map[L"NEG_INF_INT"] = NEG_INF_INT;
  ident_map[L"NAN_FLOAT"] = NAN_FLOAT;
  ident_map[L"INF_FLOAT"] = INF_FLOAT;
  ident_map[L"NEG_INF_FLOAT"] = NEG_INF_FLOAT;
  ident_map[L"LOAD_CLS_INST_ID"] = LOAD_CLS_INST_ID;
  ident_map[L"STRING_HASH_ID"] = STRING_HASH_ID;
  ident_map[L"LOAD_CLS_BY_INST"] = LOAD_CLS_BY_INST;
  ident_map[L"LOAD_NEW_OBJ_INST"] = LOAD_NEW_OBJ_INST;
  ident_map[L"LOAD_INST_UID"] = LOAD_INST_UID;
  ident_map[L"LOAD_MULTI_ARY_SIZE"] = LOAD_MULTI_ARY_SIZE;
  // standard i/o
  ident_map[L"STD_IN_STRING"] = STD_IN_STRING;
  ident_map[L"STD_OUT_BOOL"] = STD_OUT_BOOL;
  ident_map[L"STD_OUT_BYTE"] = STD_OUT_BYTE;
  ident_map[L"STD_OUT_CHAR"] = STD_OUT_CHAR;
  ident_map[L"STD_OUT_INT"] = STD_OUT_INT;
  ident_map[L"STD_OUT_FLOAT"] = STD_OUT_FLOAT;
	ident_map[L"STD_INT_FMT"] = STD_INT_FMT;
	ident_map[L"STD_FLOAT_FMT"] = STD_FLOAT_FMT;
	ident_map[L"STD_FLOAT_PER"] = STD_FLOAT_PER;
	ident_map[L"STD_WIDTH"] = STD_WIDTH;
	ident_map[L"STD_FILL"] = STD_FILL;
  ident_map[L"STD_OUT_STRING"] = STD_OUT_STRING;
  ident_map[L"STD_OUT_BYTE_ARY_LEN"] = STD_OUT_BYTE_ARY_LEN;
  ident_map[L"STD_OUT_CHAR_ARY_LEN"] = STD_OUT_CHAR_ARY_LEN;
  ident_map[L"STD_IN_BYTE_ARY_LEN"] = STD_IN_BYTE_ARY_LEN;
  ident_map[L"STD_IN_CHAR_ARY_LEN"] = STD_IN_CHAR_ARY_LEN;
  // standard error i/o
  ident_map[L"STD_ERR_BOOL"] = STD_ERR_BOOL;
  ident_map[L"STD_ERR_BYTE"] = STD_ERR_BYTE;
  ident_map[L"STD_ERR_CHAR"] = STD_ERR_CHAR;
  ident_map[L"STD_ERR_INT"] = STD_ERR_INT;
  ident_map[L"STD_ERR_FLOAT"] = STD_ERR_FLOAT;
  ident_map[L"STD_ERR_STRING"] = STD_ERR_STRING;
  ident_map[L"STD_ERR_CHAR_ARY"] = STD_ERR_CHAR_ARY;
  ident_map[L"STD_ERR_BYTE_ARY"] = STD_ERR_BYTE_ARY;
  ident_map[L"STD_FLUSH"] = STD_FLUSH;
  ident_map[L"STD_ERR_FLUSH"] = STD_ERR_FLUSH;  
  // compression
  ident_map[L"COMPRESS_ZLIB_BYTES"] = COMPRESS_ZLIB_BYTES;
  ident_map[L"UNCOMPRESS_ZLIB_BYTES"] = UNCOMPRESS_ZLIB_BYTES;
  ident_map[L"COMPRESS_GZIP_BYTES"] = COMPRESS_GZIP_BYTES;
  ident_map[L"UNCOMPRESS_GZIP_BYTES"] = UNCOMPRESS_GZIP_BYTES;
  ident_map[L"COMPRESS_BR_BYTES"] = COMPRESS_BR_BYTES;
  ident_map[L"UNCOMPRESS_BR_BYTES"] = UNCOMPRESS_BR_BYTES;
  ident_map[L"CRC32_BYTES"] = CRC32_BYTES;
  // file i/o
  ident_map[L"FILE_OPEN_READ"] = FILE_OPEN_READ;
  ident_map[L"FILE_OPEN_APPEND"] = FILE_OPEN_APPEND;
  ident_map[L"FILE_CLOSE"] = FILE_CLOSE;
  ident_map[L"FILE_FLUSH"] = FILE_FLUSH;
  ident_map[L"FILE_IN_BYTE"] = FILE_IN_BYTE;
  ident_map[L"FILE_IN_BYTE_ARY"] = FILE_IN_BYTE_ARY;
  ident_map[L"FILE_IN_CHAR_ARY"] = FILE_IN_CHAR_ARY;
  ident_map[L"FILE_IN_STRING"] = FILE_IN_STRING;
  ident_map[L"FILE_OUT_BYTE"] = FILE_OUT_BYTE;
  ident_map[L"FILE_OUT_BYTE_ARY"] = FILE_OUT_BYTE_ARY;
  ident_map[L"FILE_OUT_CHAR_ARY"] = FILE_OUT_CHAR_ARY;
  ident_map[L"FILE_OPEN_WRITE"] = FILE_OPEN_WRITE;
  ident_map[L"FILE_OPEN_READ_WRITE"] = FILE_OPEN_READ_WRITE;
  ident_map[L"FILE_OUT_BYTE"] = FILE_OUT_BYTE;
  ident_map[L"FILE_OUT_STRING"] = FILE_OUT_STRING;
  ident_map[L"FILE_EXISTS"] = FILE_EXISTS;
  ident_map[L"FILE_CAN_READ_ONLY"] = FILE_CAN_READ_ONLY;
  ident_map[L"FILE_CAN_WRITE_ONLY"] = FILE_CAN_WRITE_ONLY;
  ident_map[L"FILE_CAN_READ_WRITE"] = FILE_CAN_READ_WRITE;
  ident_map[L"FILE_SIZE"] = FILE_SIZE;
  ident_map[L"FILE_FULL_PATH"] = FILE_FULL_PATH;
  ident_map[L"FILE_TEMP_NAME"] = FILE_TEMP_NAME;
  ident_map[L"FILE_SEEK"] = FILE_SEEK;
  ident_map[L"FILE_EOF"] = FILE_EOF;
  ident_map[L"FILE_REWIND"] = FILE_REWIND;
  ident_map[L"FILE_IS_OPEN"] = FILE_IS_OPEN;
  ident_map[L"FILE_DELETE"] = FILE_DELETE;
  ident_map[L"FILE_RENAME"] = FILE_RENAME;
  ident_map[L"FILE_COPY"] = FILE_COPY;
  ident_map[L"PIPE_OPEN"] = PIPE_OPEN;
  ident_map[L"PIPE_CREATE"] = PIPE_CREATE;
  ident_map[L"PIPE_IN_BYTE"] = PIPE_IN_BYTE;
  ident_map[L"PIPE_OUT_BYTE"] = PIPE_OUT_BYTE;
  ident_map[L"PIPE_IN_BYTE_ARY"] = PIPE_IN_BYTE_ARY;
  ident_map[L"PIPE_IN_CHAR_ARY"] = PIPE_IN_CHAR_ARY;
  ident_map[L"PIPE_OUT_BYTE_ARY"] = PIPE_OUT_BYTE_ARY;
  ident_map[L"PIPE_OUT_CHAR_ARY"] = PIPE_OUT_CHAR_ARY;
  ident_map[L"PIPE_IN_STRING"] = PIPE_IN_STRING;
  ident_map[L"PIPE_OUT_STRING"] = PIPE_OUT_STRING;
  ident_map[L"PIPE_CLOSE"] = PIPE_CLOSE;
  ident_map[L"DIR_CREATE"] = DIR_CREATE;
  ident_map[L"DIR_SLASH"] = DIR_SLASH;
  ident_map[L"DIR_EXISTS"] = DIR_EXISTS;
  ident_map[L"DIR_COPY"] = DIR_COPY;
  ident_map[L"DIR_LIST"] = DIR_LIST;
  ident_map[L"DIR_DELETE"] = DIR_DELETE;
  ident_map[L"DIR_GET_CUR"] = DIR_GET_CUR;
  ident_map[L"DIR_SET_CUR"] = DIR_SET_CUR;
  ident_map[L"SYM_LINK_CREATE"] = SYM_LINK_CREATE;
  ident_map[L"SYM_LINK_COPY"] = SYM_LINK_COPY;
  ident_map[L"SYM_LINK_LOC"] = SYM_LINK_LOC;
  ident_map[L"SYM_LINK_EXISTS"] = SYM_LINK_EXISTS;
  ident_map[L"HARD_LINK_CREATE"] = HARD_LINK_CREATE;
  ident_map[L"ASYNC_MTHD_CALL"] = ASYNC_MTHD_CALL;
  ident_map[L"EXT_LIB_LOAD"] = EXT_LIB_LOAD;
  ident_map[L"EXT_LIB_UNLOAD"] = EXT_LIB_UNLOAD;
  ident_map[L"EXT_LIB_FUNC_CALL"] = EXT_LIB_FUNC_CALL;
  ident_map[L"THREAD_MUTEX"] = THREAD_MUTEX;
  ident_map[L"THREAD_SLEEP"] = THREAD_SLEEP;
  ident_map[L"THREAD_JOIN"] = THREAD_JOIN;
  ident_map[L"BYTES_TO_UNICODE"] = BYTES_TO_UNICODE;
  ident_map[L"UNICODE_TO_BYTES"] = UNICODE_TO_BYTES;
  ident_map[L"SYS_TIME"] = SYS_TIME;
  ident_map[L"GMT_TIME"] = GMT_TIME;
  ident_map[L"FILE_CREATE_TIME"] = FILE_CREATE_TIME;
  ident_map[L"FILE_MODIFIED_TIME"] = FILE_MODIFIED_TIME;
  ident_map[L"FILE_ACCESSED_TIME"] = FILE_ACCESSED_TIME;
  ident_map[L"FILE_ACCOUNT_OWNER"] = FILE_ACCOUNT_OWNER;
  ident_map[L"FILE_GROUP_OWNER"] = FILE_GROUP_OWNER;
  ident_map[L"DATE_TIME_SET_1"] = DATE_TIME_SET_1;
  ident_map[L"DATE_TIME_SET_2"] = DATE_TIME_SET_2;
  ident_map[L"DATE_TIME_ADD_DAYS"] = DATE_TIME_ADD_DAYS;
  ident_map[L"DATE_TIME_ADD_HOURS"] = DATE_TIME_ADD_HOURS;
  ident_map[L"DATE_TIME_ADD_MINS"] = DATE_TIME_ADD_MINS;
  ident_map[L"DATE_TIME_ADD_SECS"] = DATE_TIME_ADD_SECS; 
  ident_map[L"DATE_TO_UNIX_TIME"] = DATE_TO_UNIX_TIME;
  ident_map[L"DATE_FROM_UNIX_GMT_TIME"] = DATE_FROM_UNIX_GMT_TIME;
  ident_map[L"DATE_FROM_UNIX_LOCAL_TIME"] = DATE_FROM_UNIX_LOCAL_TIME;
  ident_map[L"GET_PLTFRM"] = GET_PLTFRM;
  ident_map[L"GET_UUID"] = GET_UUID;
  ident_map[L"GET_VERSION"] = GET_VERSION;
  ident_map[L"GET_SYS_PROP"] = GET_SYS_PROP;
  ident_map[L"SET_SYS_PROP"] = SET_SYS_PROP;
  ident_map[L"GET_SYS_ENV"] = GET_SYS_ENV;
  ident_map[L"SET_SYS_ENV"] = SET_SYS_ENV;
  ident_map[L"ASSERT_TRUE"] = ASSERT_TRUE;
  ident_map[L"SYS_CMD"] = SYS_CMD;
  ident_map[L"SYS_CMD_OUT"] = SYS_CMD_OUT;
  ident_map[L"SET_SIGNAL"] = SET_SIGNAL;
  ident_map[L"RAISE_SIGNAL"] = RAISE_SIGNAL;
  ident_map[L"EXIT"] = EXIT;
  ident_map[L"TIMER_START"] = TIMER_START;
  ident_map[L"TIMER_END"] =  TIMER_END;
  ident_map[L"TIMER_ELAPSED"] =  TIMER_ELAPSED;
  ident_map[L"SOCK_TCP_CONNECT"] = SOCK_TCP_CONNECT;
  ident_map[L"SOCK_TCP_IS_CONNECTED"] = SOCK_TCP_IS_CONNECTED;
  ident_map[L"SOCK_TCP_BIND"] = SOCK_TCP_BIND;
  ident_map[L"SOCK_UDP_CREATE"] = SOCK_UDP_CREATE;
  ident_map[L"SOCK_UDP_BIND"] = SOCK_UDP_BIND;
  ident_map[L"SOCK_UDP_CLOSE"] = SOCK_UDP_CLOSE;
  ident_map[L"SOCK_UDP_IN_BYTE"] = SOCK_UDP_IN_BYTE;
  ident_map[L"SOCK_UDP_IN_BYTE_ARY"] = SOCK_UDP_IN_BYTE_ARY;
  ident_map[L"SOCK_UDP_IN_CHAR_ARY"] = SOCK_UDP_IN_CHAR_ARY;
  ident_map[L"SOCK_UDP_OUT_BYTE"] = SOCK_UDP_OUT_BYTE;
  ident_map[L"SOCK_UDP_OUT_BYTE_ARY"] = SOCK_UDP_OUT_BYTE_ARY;
  ident_map[L"SOCK_UDP_OUT_CHAR_ARY"] = SOCK_UDP_OUT_CHAR_ARY;
  ident_map[L"SOCK_UDP_IN_STRING"] = SOCK_UDP_IN_STRING;
  ident_map[L"SOCK_UDP_OUT_STRING"] = SOCK_UDP_OUT_STRING;
  ident_map[L"SOCK_TCP_SSL_LISTEN"] = SOCK_TCP_SSL_LISTEN;
  ident_map[L"SOCK_TCP_SSL_ACCEPT"] = SOCK_TCP_SSL_ACCEPT;
  ident_map[L"SOCK_TCP_SSL_SELECT"] = SOCK_TCP_SSL_SELECT;
  ident_map[L"SOCK_TCP_SSL_ERROR"] = SOCK_TCP_SSL_ERROR;
  ident_map[L"SOCK_IP_ERROR"] = SOCK_IP_ERROR;
  ident_map[L"SOCK_TCP_SSL_SRV_CLOSE"] = SOCK_TCP_SSL_SRV_CLOSE;
  ident_map[L"SOCK_TCP_SSL_SRV_CERT"] = SOCK_TCP_SSL_SRV_CERT;
  ident_map[L"SOCK_TCP_LISTEN"] = SOCK_TCP_LISTEN;
  ident_map[L"SOCK_TCP_ACCEPT"] = SOCK_TCP_ACCEPT;
  ident_map[L"SOCK_TCP_SELECT"] = SOCK_TCP_SELECT;
  ident_map[L"SOCK_TCP_CLOSE"] = SOCK_TCP_CLOSE;
  ident_map[L"SOCK_TCP_IN_BYTE"] = SOCK_TCP_IN_BYTE;
  ident_map[L"SOCK_TCP_IN_BYTE_ARY"] = SOCK_TCP_IN_BYTE_ARY;
  ident_map[L"SOCK_TCP_IN_CHAR_ARY"] = SOCK_TCP_IN_CHAR_ARY;
  ident_map[L"SOCK_TCP_OUT_STRING"] = SOCK_TCP_OUT_STRING;
  ident_map[L"SOCK_TCP_IN_STRING"] = SOCK_TCP_IN_STRING;
  ident_map[L"SOCK_TCP_OUT_BYTE"] = SOCK_TCP_OUT_BYTE;
  ident_map[L"SOCK_TCP_OUT_BYTE_ARY"] = SOCK_TCP_OUT_BYTE_ARY;
  ident_map[L"SOCK_TCP_OUT_CHAR_ARY"] = SOCK_TCP_OUT_CHAR_ARY;
  ident_map[L"SOCK_TCP_HOST_NAME"] = SOCK_TCP_HOST_NAME;
  ident_map[L"SOCK_TCP_RESOLVE_NAME"] = SOCK_TCP_RESOLVE_NAME;
  ident_map[L"SOCK_TCP_SSL_CONNECT"] = SOCK_TCP_SSL_CONNECT;
  ident_map[L"SOCK_TCP_SSL_ISSUER"] = SOCK_TCP_SSL_ISSUER;
  ident_map[L"SOCK_TCP_SSL_SUBJECT"] = SOCK_TCP_SSL_SUBJECT;
  ident_map[L"SOCK_TCP_SSL_CLOSE"] = SOCK_TCP_SSL_CLOSE;
  ident_map[L"SOCK_TCP_SSL_IN_BYTE"] = SOCK_TCP_SSL_IN_BYTE;
  ident_map[L"SOCK_TCP_SSL_IN_BYTE_ARY"] = SOCK_TCP_SSL_IN_BYTE_ARY;
  ident_map[L"SOCK_TCP_SSL_IN_CHAR_ARY"] = SOCK_TCP_SSL_IN_CHAR_ARY;
  ident_map[L"SOCK_TCP_SSL_OUT_STRING"] = SOCK_TCP_SSL_OUT_STRING;
  ident_map[L"SOCK_TCP_SSL_IN_STRING"] = SOCK_TCP_SSL_IN_STRING;
  ident_map[L"SOCK_TCP_SSL_OUT_BYTE"] = SOCK_TCP_SSL_OUT_BYTE;
  ident_map[L"SOCK_TCP_SSL_OUT_BYTE_ARY"] = SOCK_TCP_SSL_OUT_BYTE_ARY;
  ident_map[L"SOCK_TCP_SSL_OUT_CHAR_ARY"] = SOCK_TCP_SSL_OUT_CHAR_ARY;
  ident_map[L"SERL_INT"] = SERL_INT;
  ident_map[L"SERL_FLOAT"] = SERL_FLOAT;
  ident_map[L"SERL_OBJ_INST"] = SERL_OBJ_INST;
  ident_map[L"SERL_BYTE_ARY"] = SERL_BYTE_ARY;  
  ident_map[L"SERL_CHAR_ARY"] = SERL_CHAR_ARY;
  ident_map[L"SERL_CHAR"] = SERL_CHAR;
  ident_map[L"SERL_INT_ARY"] = SERL_INT_ARY;
  ident_map[L"SERL_OBJ_ARY"] = SERL_OBJ_ARY;  
  ident_map[L"SERL_FLOAT_ARY"] = SERL_FLOAT_ARY;
  ident_map[L"DESERL_INT"] = DESERL_INT;
  ident_map[L"DESERL_FLOAT"] = DESERL_FLOAT;
  ident_map[L"DESERL_OBJ_INST"] = DESERL_OBJ_INST;
  ident_map[L"DESERL_BYTE_ARY"] = DESERL_BYTE_ARY;
  ident_map[L"DESERL_CHAR_ARY"] = DESERL_CHAR_ARY;
  ident_map[L"DESERL_CHAR"] = DESERL_CHAR;
  ident_map[L"DESERL_INT_ARY"] = DESERL_INT_ARY;
  ident_map[L"DESERL_OBJ_ARY"] = DESERL_OBJ_ARY;
  ident_map[L"DESERL_FLOAT_ARY"] = DESERL_FLOAT_ARY;
#endif
}

/****************************
 * Processes language
 * identifies
 ****************************/
void Scanner::CheckIdentifier(int index)
{
  try {
    // copy string
    const size_t length = end_pos - start_pos;
    std::wstring ident(buffer, start_pos, length);
    // check string
    ScannerTokenType ident_type = ident_map[ident];
    switch(ident_type) {
    case TOKEN_AND_ID:
    case TOKEN_NOT_ID:
    case TOKEN_OR_ID:
    case TOKEN_XOR_ID:
    case TOKEN_CRITICAL_ID:
    case TOKEN_VIRTUAL_ID:
    case TOKEN_FROM_ID:
    case TOKEN_OTHER_ID:
    case TOKEN_ENUM_ID:
    case TOKEN_CONSTS_ID:
    case TOKEN_FOR_ID:
    case TOKEN_EACH_ID:
    case TOKEN_IN_ID:
    case TOKEN_REVERSE_ID:
    case TOKEN_SELECT_ID:
    case TOKEN_LABEL_ID:
    case TOKEN_NATIVE_ID:
    case TOKEN_IF_ID:
    case TOKEN_ELSE_ID:
    case TOKEN_DO_ID:
    case TOKEN_WHILE_ID:
    case TOKEN_BREAK_ID:
    case TOKEN_CONTINUE_ID:
    case TOKEN_BOOLEAN_ID:
    case TOKEN_TRUE_ID:
    case TOKEN_FALSE_ID:
    case TOKEN_USE_ID:
    case TOKEN_BUNDLE_ID:
    case TOKEN_STATIC_ID:
    case TOKEN_PUBLIC_ID:
    case TOKEN_PRIVATE_ID:
    case TOKEN_AS_ID:
    case TOKEN_TYPE_OF_ID:
    case TOKEN_PARENT_ID:
    case TOKEN_CLASS_ID:
    case TOKEN_INTERFACE_ID:
    case TOKEN_ALIAS_ID:
    case TOKEN_IMPLEMENTS_ID:
    case TOKEN_FUNCTION_ID:
    case TOKEN_METHOD_ID:
    case TOKEN_BYTE_ID:
    case TOKEN_INT_ID:
    case TOKEN_RETURN_ID:
    case TOKEN_LEAVING_ID:
    case TOKEN_FLOAT_ID:
    case TOKEN_CHAR_ID:
    case TOKEN_NEW_ID:
    case TOKEN_NIL_ID:
#ifdef _SYSTEM
    case S2I:
    case S2F:
    case I2S:
    case F2S:
    case LOAD_ARY_SIZE:
    case CPY_BYTE_ARY:
    case CPY_CHAR_ARY:
    case CPY_INT_ARY:
    case CPY_FLOAT_ARY:
    case ZERO_BYTE_ARY:
    case ZERO_CHAR_ARY:
    case ZERO_INT_ARY:
    case ZERO_FLOAT_ARY:
    case FLOR_FLOAT:
    case CEIL_FLOAT:
    case TRUNC_FLOAT:
    case SIN_FLOAT:
    case COS_FLOAT:
    case TAN_FLOAT:
    case ASIN_FLOAT:
    case ACOSH_FLOAT:
    case ASINH_FLOAT:
    case ATANH_FLOAT:
    case COSH_FLOAT:
    case SINH_FLOAT:
    case TANH_FLOAT:
    case ACOS_FLOAT:
    case ATAN_FLOAT:
    case CBRT_FLOAT:
    case LOG2_FLOAT:
    case ATAN2_FLOAT:
    case MOD_FLOAT:
    case LOG_FLOAT:
    case ROUND_FLOAT:
    case EXP_FLOAT:
    case LOG10_FLOAT:
    case POW_FLOAT:
    case SQRT_FLOAT:
    case GAMMA_FLOAT:
    case NAN_INT:
    case INF_INT:
    case NEG_INF_INT:
    case NAN_FLOAT:
    case INF_FLOAT:
    case NEG_INF_FLOAT:
    case RAND_FLOAT:
    case LOAD_CLS_INST_ID:
    case STRING_HASH_ID:
    case LOAD_CLS_BY_INST:
    case LOAD_NEW_OBJ_INST:
    case LOAD_INST_UID:
    case LOAD_MULTI_ARY_SIZE:
    case STD_IN_STRING:
    case STD_OUT_STRING:
    case STD_OUT_BYTE_ARY_LEN:
    case STD_OUT_CHAR_ARY_LEN:
    case STD_IN_BYTE_ARY_LEN:
    case STD_IN_CHAR_ARY_LEN:
    case STD_OUT_BOOL:
    case STD_OUT_BYTE:
    case STD_OUT_CHAR:
    case STD_OUT_INT:
    case STD_OUT_FLOAT:
    case STD_INT_FMT:
    case STD_FLOAT_FMT:
    case STD_FLOAT_PER:
    case STD_WIDTH:
    case STD_FILL:
    case STD_FLUSH:
    case STD_ERR_FLUSH:
    case STD_ERR_STRING:
    case STD_ERR_CHAR_ARY:
    case STD_ERR_BYTE_ARY:
    case STD_ERR_BOOL:
    case STD_ERR_BYTE:
    case STD_ERR_CHAR:
    case STD_ERR_INT:
    case STD_ERR_FLOAT:
    case COMPRESS_ZLIB_BYTES:
    case UNCOMPRESS_ZLIB_BYTES:
    case COMPRESS_GZIP_BYTES:
    case UNCOMPRESS_GZIP_BYTES:
    case COMPRESS_BR_BYTES:
    case UNCOMPRESS_BR_BYTES:
    case CRC32_BYTES:
    case FILE_OPEN_READ:
    case FILE_OPEN_APPEND:
    case FILE_CLOSE:
    case FILE_FLUSH:
    case FILE_IN_BYTE:
    case FILE_IN_BYTE_ARY:
    case FILE_IN_CHAR_ARY:
    case FILE_IN_STRING:
    case FILE_OPEN_WRITE:
    case FILE_OPEN_READ_WRITE:
    case FILE_OUT_BYTE:
    case FILE_OUT_BYTE_ARY:
    case FILE_OUT_CHAR_ARY:
    case FILE_OUT_STRING:
    case FILE_EXISTS:
    case FILE_CAN_WRITE_ONLY:
    case FILE_CAN_READ_ONLY:
    case FILE_CAN_READ_WRITE:
    case FILE_FULL_PATH:
    case FILE_TEMP_NAME:
    case FILE_SIZE:
    case FILE_SEEK:
    case FILE_EOF:
    case FILE_REWIND:
    case FILE_IS_OPEN:
    case FILE_DELETE:
    case FILE_RENAME:
    case FILE_COPY:
    case PIPE_OPEN:
    case PIPE_CREATE:
    case PIPE_IN_BYTE:
    case PIPE_OUT_BYTE:
    case PIPE_IN_BYTE_ARY:
    case PIPE_IN_CHAR_ARY:
    case PIPE_OUT_BYTE_ARY:
    case PIPE_OUT_CHAR_ARY:
    case PIPE_IN_STRING:
    case PIPE_OUT_STRING:
    case PIPE_CLOSE:
    case DIR_CREATE:
    case DIR_SLASH:
    case DIR_EXISTS:
    case DIR_LIST:
    case DIR_COPY:
    case DIR_GET_CUR:
    case DIR_SET_CUR:
    case DIR_DELETE:
    case SYM_LINK_CREATE:
    case SYM_LINK_COPY:
    case SYM_LINK_LOC:
    case SYM_LINK_EXISTS:
    case HARD_LINK_CREATE:
    case ASYNC_MTHD_CALL:
    case EXT_LIB_LOAD:
    case EXT_LIB_UNLOAD:
    case EXT_LIB_FUNC_CALL:
    case THREAD_MUTEX:
    case THREAD_SLEEP:
    case THREAD_JOIN:
    case SYS_TIME:
    case BYTES_TO_UNICODE:
    case UNICODE_TO_BYTES:
    case GMT_TIME:
    case FILE_CREATE_TIME:
    case FILE_MODIFIED_TIME:
    case FILE_ACCESSED_TIME:
    case FILE_ACCOUNT_OWNER:
    case FILE_GROUP_OWNER:
    case DATE_TIME_SET_1:
    case DATE_TIME_SET_2:
    case DATE_TIME_ADD_DAYS:
    case DATE_TIME_ADD_HOURS:
    case DATE_TIME_ADD_MINS:
    case DATE_TIME_ADD_SECS:
    case DATE_TO_UNIX_TIME:
    case DATE_FROM_UNIX_GMT_TIME:
    case DATE_FROM_UNIX_LOCAL_TIME:
    case GET_PLTFRM:
    case GET_UUID:
    case GET_VERSION:
    case GET_SYS_PROP:
    case GET_SYS_ENV:
    case SET_SYS_ENV:
    case SET_SYS_PROP:
    case ASSERT_TRUE:
    case SYS_CMD:
    case SYS_CMD_OUT:
    case SET_SIGNAL:
    case RAISE_SIGNAL:
    case EXIT:
    case TIMER_START:
    case TIMER_END:
    case TIMER_ELAPSED:
    case SOCK_TCP_CONNECT:
    case SOCK_TCP_BIND:
    case SOCK_UDP_CREATE:
    case SOCK_UDP_BIND:
    case SOCK_UDP_CLOSE:
    case SOCK_UDP_IN_BYTE:
    case SOCK_UDP_IN_BYTE_ARY:
    case SOCK_UDP_IN_CHAR_ARY:
    case SOCK_UDP_OUT_BYTE:
    case SOCK_UDP_OUT_BYTE_ARY:
    case SOCK_UDP_OUT_CHAR_ARY:
    case SOCK_UDP_IN_STRING:
    case SOCK_UDP_OUT_STRING:
    case SOCK_TCP_SSL_LISTEN:
    case SOCK_TCP_SSL_ACCEPT:
    case SOCK_TCP_SSL_SELECT:
    case SOCK_TCP_SSL_SRV_CERT:
	 case SOCK_TCP_SSL_ERROR:
	 case SOCK_IP_ERROR:
    case SOCK_TCP_SSL_SRV_CLOSE:
    case SOCK_TCP_LISTEN:
    case SOCK_TCP_ACCEPT:
    case SOCK_TCP_SELECT:
    case SOCK_TCP_IS_CONNECTED:
    case SOCK_TCP_CLOSE:
    case SOCK_TCP_IN_BYTE:
    case SOCK_TCP_IN_BYTE_ARY:
    case SOCK_TCP_IN_CHAR_ARY:
    case SOCK_TCP_IN_STRING:
    case SOCK_TCP_OUT_STRING:
    case SOCK_TCP_OUT_BYTE:
    case SOCK_TCP_OUT_BYTE_ARY:
    case SOCK_TCP_OUT_CHAR_ARY:
    case SOCK_TCP_HOST_NAME:
    case SOCK_TCP_RESOLVE_NAME:
    case SOCK_TCP_SSL_CONNECT:
    case SOCK_TCP_SSL_ISSUER:
    case SOCK_TCP_SSL_SUBJECT:
    case SOCK_TCP_SSL_CLOSE:
    case SOCK_TCP_SSL_IN_BYTE:
    case SOCK_TCP_SSL_IN_BYTE_ARY:
    case SOCK_TCP_SSL_IN_CHAR_ARY:
    case SOCK_TCP_SSL_IN_STRING:
    case SOCK_TCP_SSL_OUT_STRING:
    case SOCK_TCP_SSL_OUT_BYTE:
    case SOCK_TCP_SSL_OUT_BYTE_ARY:
    case SOCK_TCP_SSL_OUT_CHAR_ARY:
    case SERL_INT:
    case SERL_FLOAT:
    case SERL_OBJ_INST:
    case SERL_BYTE_ARY:
    case SERL_CHAR_ARY:
    case SERL_CHAR:
    case SERL_INT_ARY:
    case SERL_OBJ_ARY:
    case SERL_FLOAT_ARY:
    case DESERL_INT:
    case DESERL_FLOAT:
    case DESERL_OBJ_INST:
    case DESERL_BYTE_ARY:
    case DESERL_CHAR_ARY:
    case DESERL_CHAR:
    case DESERL_INT_ARY:
    case DESERL_OBJ_ARY:      
    case DESERL_FLOAT_ARY:
#endif
      tokens[index]->SetType(ident_type);
      break;

    default:
      tokens[index]->SetType(TOKEN_IDENT);
      tokens[index]->SetIdentifier(ident);
      break;
    }
    
    tokens[index]->SetLineNbr(line_nbr);
	  tokens[index]->SetLinePos((int)(line_pos - length - 1));
    tokens[index]->SetFileName(filename);
  }
  catch(const std::out_of_range&) {
    tokens[index]->SetType(TOKEN_UNKNOWN);
    tokens[index]->SetLineNbr(line_nbr);
    tokens[index]->SetLinePos((int)(line_pos - 1));
    tokens[index]->SetFileName(filename);
  }
}

void Scanner::CheckString(int index, bool is_lit, bool is_valid)
{
  // copy string
  const size_t length = end_pos - start_pos;
  std::wstring char_string(buffer, start_pos, length);
  // set string
  if(is_valid) {
    tokens[index]->SetType(TOKEN_CHAR_STRING_LIT);
  }
  else {
    tokens[index]->SetType(TOKEN_BAD_CHAR_STRING_LIT);
  }
  tokens[index]->SetByteLit(is_lit ? 1 : 0);

  tokens[index]->SetIdentifier(char_string);
  tokens[index]->SetLineNbr(line_nbr);
	tokens[index]->SetLinePos((int)(line_pos - length - 2));
  tokens[index]->SetFileName(filename);
}

void Scanner::ParseInteger(int index, int base /*= 0*/)
{
  // copy string
  const size_t length = end_pos - start_pos;
  std::wstring ident(buffer, start_pos, length);

  if(ident.back() == L'_') {
    tokens[index]->SetType(TOKEN_UNKNOWN);
  }
  else {
    ident.erase(std::remove(ident.begin(), ident.end(), L'_'), ident.end());

    // parse and check for errors
    wchar_t* ending = nullptr;
    if(base == 2) {
      tokens[index]->SetInt64Lit(wcstoll(ident.c_str() + 2, &ending, 2));
    }
    else {
      tokens[index]->SetInt64Lit(wcstoll(ident.c_str(), &ending, base));
    }

    // set token
    if(wcslen(ending)) {
      tokens[index]->SetType(TOKEN_UNKNOWN);
    }
    else {
      tokens[index]->SetType(TOKEN_INT_LIT);
    }
  }

  tokens[index]->SetLineNbr(line_nbr);
  tokens[index]->SetLinePos((int)(line_pos - length - 1));
  tokens[index]->SetFileName(filename);
}

void Scanner::ParseDouble(int index)
{
  // copy string
  const size_t length = end_pos - start_pos;
  std::wstring ident(buffer, start_pos, length);

  if(ident.back() == L'_') {
    tokens[index]->SetType(TOKEN_UNKNOWN);
  }
  else {
    ident.erase(std::remove(ident.begin(), ident.end(), L'_'), ident.end());

    // parse and check for errors
    wchar_t* ending;
    tokens[index]->SetFloatLit(wcstod(ident.c_str(), &ending));
    if(wcslen(ending)) {
      tokens[index]->SetType(TOKEN_UNKNOWN);
    }
    else {
      tokens[index]->SetType(TOKEN_FLOAT_LIT);
    }
  }

  tokens[index]->SetLineNbr(line_nbr);
  tokens[index]->SetLinePos((int)(line_pos - length - 1));
  tokens[index]->SetFileName(filename);
}

void Scanner::ParseUnicodeChar(int index)
{
  // copy string
  const size_t length = end_pos - start_pos;
  if(length < 5) {
    std::wstring ident(buffer, start_pos, length);
    // set token
    tokens[index]->SetType(TOKEN_CHAR_LIT);
    tokens[index]->SetCharLit((wchar_t)wcstol(ident.c_str(), nullptr, 16));
    tokens[index]->SetLineNbr(line_nbr);
    tokens[index]->SetLinePos((int)(line_pos - length - 1));
    tokens[index]->SetFileName(filename);
  }
  else {
    tokens[index]->SetType(TOKEN_UNKNOWN);
    tokens[index]->SetLineNbr(line_nbr);
    tokens[index]->SetLinePos((int)(line_pos - length - 1));
    tokens[index]->SetFileName(filename);
  }
}

/****************************
 * Reads a source input file
 ****************************/
void Scanner::ReadFile()
{
  buffer_pos = 0;
  is_first_token = true;
  buffer = LoadFileBuffer(filename, buffer_size);
  
#ifdef _DEBUG
  GetLogger() << L"---------- Source: '" << filename << L"' ---------" << std::endl;
#endif
}

/****************************
 * Processes the next token
 ****************************/
void Scanner::NextToken()
{
  if(!buffer) {
    for(int i = 0; i < LOOK_AHEAD; ++i) {
      tokens[i]->SetType(TOKEN_END_OF_STREAM);
      tokens[i]->SetFileName(filename);
      tokens[i]->SetLineNbr(0);
      tokens[i]->SetLinePos(0);
    };
  }
  else if(is_first_token) {
    NextChar();
    for(int i = 0; i < LOOK_AHEAD; ++i) {
      ParseToken(i);
    }
    is_first_token = false;
  } 
  else {
    int i = 1;
    for(; i < LOOK_AHEAD; ++i) {
      tokens[i - 1]->Copy(tokens[i]);
    }
    ParseToken(i - 1);
  }
}

/****************************
 * Gets the current token
 ****************************/
Token* Scanner::GetToken(int index)
{
  if(index < LOOK_AHEAD) {
    return tokens[index];
  }

  return nullptr;
}

std::wstring Scanner::RandomString(size_t len)
{
  std::random_device gen;
  const wchar_t* values = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  const size_t offset = len / 3;

  std::wstring output = L"blob://";
  for(size_t i = 0; i < len; ++i) {
    if(i != 0 && i % offset == 0) {
      output += L'-';
    }
    else {
      const size_t index = gen() % wcslen(values);
      output += values[index];
    }
  }

  return output;
}

/****************************
 * Gets the next character.
 * Note, EOB is returned at
 * end of a stream
 ****************************/
void Scanner::NextChar()
{
  if(buffer_pos < buffer_size) {
    // line number
    if(cur_char == L'\n') {
      line_nbr++;
      line_pos = 1;
    }
    // current character    
    line_pos++;
    cur_char = buffer[buffer_pos++];
    // next character
    if(buffer_pos < buffer_size) {
      nxt_char = buffer[buffer_pos];
      // next next character
      if(buffer_pos + 1 < buffer_size) {
        nxt_nxt_char = buffer[buffer_pos + 1];
      }
      // end of file
      else {
        nxt_nxt_char = EOB;
      }
    }
    // end of file
    else {
      nxt_char = EOB;
    }
  }
  // end of file
  else {
    cur_char = EOB;
  }
}

/****************************
 * Processes white space
 ****************************/
void Scanner::Whitespace()
{
  while(WHITE_SPACE && cur_char != EOB) {
    NextChar();
  }
}

/****************************
 * Processes comments
 ****************************/
void Scanner::Comments()
{
  while((cur_char == COMMENT || (cur_char == ALT_COMMENT && (nxt_char == ALT_COMMENT || nxt_char == ALT_EXTENDED_COMMENT))) && cur_char != EOB) {
    NextChar();

    // extended comment
    if(cur_char == EXTENDED_COMMENT || (cur_char == ALT_EXTENDED_COMMENT)) {
      NextChar();
      while(!((cur_char == EXTENDED_COMMENT && nxt_char == COMMENT) || (cur_char == ALT_EXTENDED_COMMENT && nxt_char == ALT_COMMENT)) && cur_char != EOB) {
        NextChar();
      }
      NextChar();
      NextChar();
    }
    // line comment
    else {
      while(cur_char != L'\n' && cur_char != EOB) {
        NextChar();
      }
    }
    Whitespace();
  }
}

/****************************
 * Parses a token
 ****************************/
void Scanner::ParseToken(int index)
{
  // unable to load buffer
  if(!buffer) {
    tokens[index]->SetType(TOKEN_NO_INPUT);
    return;
  }
  
  // ignore white space
  Whitespace();

  // ignore comments
  Comments();

  // character string
  if(cur_char == L'\"' || (cur_char == L'$' && nxt_char == L'\"')) {
    bool is_lit = false;
    if(cur_char == L'$') {
      is_lit = true;
      NextChar();
    }
    NextChar();

    // mark
    start_pos = buffer_pos - 1;
    bool is_valid = true;
    while(cur_char != L'\"' && cur_char != EOB) {
      if(cur_char == L'\\') {
        NextChar();
        if(!is_lit && !std::isdigit(cur_char)) {
          switch(cur_char) {
          case L'"':
          case L'\\':
          case L'u':
          case L'x':
          case L'X':
          case L'n':
          case L'r':
          case L'b':
          case L'a':
          case L'e':
          case L'f':
          case L't':
          case L'$':
          case L'0':
          case L'1':
          case L'2':
          case L'3':
          case L'4':
          case L'5':
          case L'6':
          case L'7':
          case L'8':
          case L'9':
            break;

          default:
            is_valid = false;
            break;
          }
        }
      }

      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    // check string
    NextChar();
    CheckString(index, is_lit, is_valid);
    return;
  }
  // character
  else if(cur_char == L'\'') {
    NextChar();
    // escape or hex/unicode encoding
    if(cur_char == L'\\') {
      NextChar();
      // read unicode string
      if(cur_char == L'u' || cur_char == L'x' || cur_char == L'X') {
        NextChar();
        start_pos = buffer_pos - 1;
        while(iswdigit(cur_char) || (cur_char >= L'a' && cur_char <= L'f') || (cur_char >= L'A' && cur_char <= L'F')) {
          NextChar();
        }
        end_pos = buffer_pos - 1;
        ParseUnicodeChar(index);
        if(cur_char != L'\'') {
          tokens[index]->SetType(TOKEN_UNKNOWN);
        }
        NextChar();
        return;
      }
      // escape
      else if(nxt_char == L'\'') {
        switch(cur_char) {
        case L'n':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\n');
          NextChar();
          NextChar();
          return;

        case L'r':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\r');
          NextChar();
          NextChar();
          return;

        case L't':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\t');
          NextChar();
          NextChar();
          return;

        case L'e':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(0x1b);
          NextChar();
          NextChar();
          return;

        case L'a':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\a');
          NextChar();
          NextChar();
          return;

        case L'b':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\b');
          NextChar();
          NextChar();
          return;

        case L'f':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\f');
          NextChar();
          NextChar();
          return;

        case L'v':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\v');
          NextChar();
          NextChar();
          return;

        case L'\\':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\\');
          NextChar();
          NextChar();
          return;

        case L'\'':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\'');
          NextChar();
          NextChar();
          return;

        case L'0':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit(L'\0');
          NextChar();
          NextChar();
          return;
        }
      }
      // error
      else {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
        return;
      }
    } else {
      // error
      if(nxt_char != L'\'') {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
        return;
      } else {
        tokens[index]->SetType(TOKEN_CHAR_LIT);
        tokens[index]->SetCharLit(cur_char);
        NextChar();
        NextChar();
        return;
      }
    }
  }
  // identifier
#ifdef _SYSTEM
  else if(iswalpha(cur_char) || cur_char == L'$' || cur_char == L'@') {
#else
  else if(iswalpha(cur_char) || cur_char == L'@') {
#endif
    // mark
    start_pos = buffer_pos - 1;

#ifdef _SYSTEM
    while((iswalpha(cur_char) || iswdigit(cur_char) || cur_char == L'_' || cur_char == L'@' || cur_char == L'$') && cur_char != EOB) {
#else
    while((iswalpha(cur_char) || iswdigit(cur_char) || cur_char == L'_' || cur_char == L'@') && cur_char != EOB) {
#endif
        NextChar();
      }
      // mark
      end_pos = buffer_pos - 1;
      // check identifier
      CheckIdentifier(index);
      return;
    }
    // number
  else if(iswdigit(cur_char) || (cur_char == L'.' && iswdigit(nxt_char))) {
    int double_state = 0;
    int hex_state = 0;
    int bin_state = 0;

    // mark
    start_pos = buffer_pos - 1;

    // test hex state
    if(cur_char == L'0' && (nxt_char == L'x' || nxt_char == L'X')) {
      hex_state = 1;
      NextChar();
    }

    // test bin state
    if(cur_char == L'0' && (nxt_char == L'b' || nxt_char == L'B')) {
      bin_state = 1;
      NextChar();
    }

    while(iswdigit(cur_char) || cur_char == L'.' || cur_char == L'_' ||
          // hex/bin format
          cur_char == L'x' || cur_char == L'X' || (cur_char >= L'a' && cur_char <= L'f') ||
          (cur_char >= L'A' && cur_char <= L'F') ||
          // scientific format
          cur_char == L'e' || cur_char == L'E' || 
          (double_state == 2 && (cur_char == L'+' || cur_char == L'-') && iswdigit(nxt_char)))  {
      // decimal double
      if(cur_char == L'.') {
        // error
        if(double_state || hex_state || bin_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
          break;
        }
        double_state = 1;
      }
      else if(!hex_state && !bin_state && (cur_char == L'e' || cur_char == L'E')) {
        // error
        if(double_state != 1) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
          break;
        }
        double_state = 2;
      }
      else if(double_state == 2 && (cur_char == L'+' || cur_char == L'-')) {
        double_state++;
      }      
      // hex integer
      else if(cur_char == L'x' || cur_char == L'X') {
        // error
        if(double_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
          break;
        }

        if(hex_state == 1) {
          hex_state = 2;
        }
        else {
          hex_state = 1;
        }
      }
      // bin integer
      else if(cur_char == L'b' || cur_char == L'B') {
        // error
        if(double_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
          break;
        }

        if(bin_state == 1) {
          bin_state = 2;
        }
        else {
          bin_state = 1;
        }
      }

      // next character
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    // state 1, 2 or 3
    if(double_state > 0 && double_state < 4) {
      ParseDouble(index);
    } 
    else if(hex_state == 2) {
      ParseInteger(index, 16);
    }
    else if(bin_state == 2) {
      ParseInteger(index, 2);
    }
    else if(hex_state || bin_state || double_state) {
      tokens[index]->SetType(TOKEN_UNKNOWN);
    }
    else {
      ParseInteger(index);
    }
    return;
  }
  // other
  else {
    tokens[index]->SetFileName(filename);
    tokens[index]->SetLineNbr(line_nbr);
    tokens[index]->SetLinePos((int)(line_pos - 1));
      
    switch(cur_char) {
    case L':':
      if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_ASSIGN);
        NextChar();
      } 
      else {
        tokens[index]->SetType(TOKEN_COLON);
        NextChar();
      }
      break;

    case L'-':
      if(nxt_char == L'>') {
        NextChar();
        tokens[index]->SetType(TOKEN_ASSESSOR);
        NextChar();
      } 
      else if(nxt_char == L'-') {
        NextChar();
        tokens[index]->SetType(TOKEN_SUB_SUB);
        NextChar();
      }
      else if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_SUB_ASSIGN);
        NextChar();
      } 
      else {
        tokens[index]->SetType(TOKEN_SUB);
        NextChar();
      }
      break;
        
    case L'!':
      if(alt_syntax && nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_NEQL);
        NextChar();
      }
      else if(alt_syntax) {
        tokens[index]->SetType(TOKEN_NOT);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
      }
      break;
        
    case L'{':
      tokens[index]->SetType(TOKEN_OPEN_BRACE);
      NextChar();
      break;

    case L'.':
      tokens[index]->SetType(TOKEN_PERIOD);
      NextChar();
      break;

    case L'~':
      tokens[index]->SetType(TOKEN_TILDE);
      NextChar();
      break;

    case L'\\':
      tokens[index]->SetType(TOKEN_BACK_SLASH);
      NextChar();
      break;

    case L'}':
      tokens[index]->SetType(TOKEN_CLOSED_BRACE);
      NextChar();
      break;

    case L'[':
      tokens[index]->SetType(TOKEN_OPEN_BRACKET);
      NextChar();
      break;

    case L']':
      tokens[index]->SetType(TOKEN_CLOSED_BRACKET);
      NextChar();
      break;

    case L'(':
      tokens[index]->SetType(TOKEN_OPEN_PAREN);
      NextChar();
      break;

    case L')':
      tokens[index]->SetType(TOKEN_CLOSED_PAREN);
      NextChar();
      break;

    case L',':
      tokens[index]->SetType(TOKEN_COMMA);
      NextChar();
      break;

    case L';':
      tokens[index]->SetType(TOKEN_SEMI_COLON);
      NextChar();
      break;

    case L'^':
      tokens[index]->SetType(TOKEN_HAT);
      NextChar();
      break;

    case L'&':
      if(alt_syntax && nxt_char == L'&') {
        NextChar();
        tokens[index]->SetType(TOKEN_AND);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_AND);
        NextChar();
      }
      break;
        
    case L'|':
      if(alt_syntax && nxt_char == L'|') {
        NextChar();
        tokens[index]->SetType(TOKEN_OR);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_OR);
        NextChar();
      }
      break;

    case L'?':
      tokens[index]->SetType(TOKEN_QUESTION);
      NextChar();
      break;

    case L'=':
      if(alt_syntax) {
        if(nxt_char == L'=') {
          NextChar();
          tokens[index]->SetType(TOKEN_EQL);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_ASSIGN);
          NextChar();
        }
      }
      else if(nxt_char == L'>') {
        NextChar();
        tokens[index]->SetType(TOKEN_LAMBDA);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_EQL);
        NextChar();
      }
      break;

    case L'<':
      if(nxt_char == L'>') {
        NextChar();
        if(alt_syntax) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_NEQL);
          NextChar();
        }
      } 
      else if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_LEQL);
        NextChar();
      } 
      else if(nxt_char == L'<') {
        NextChar();          
        tokens[index]->SetType(TOKEN_SHL);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_LES);
        NextChar();
      }
      break;

    case L'>':
      if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_GEQL);
        NextChar();
      }
      else if(nxt_char == L'>') {
        NextChar();
        tokens[index]->SetType(TOKEN_SHR);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_GTR);
        NextChar();
      }
      break;

    case L'+':
      if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_ADD_ASSIGN);
        NextChar();
      }
      else if(nxt_char == L'+') {
        NextChar();
        tokens[index]->SetType(TOKEN_ADD_ADD);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_ADD);
        NextChar();
      }
      break;

    case L'*':
      if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_MUL_ASSIGN);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_MUL);
        NextChar();
      }
      break;
        
    case L'/':
      if(nxt_char == L'=') {
        NextChar();
        tokens[index]->SetType(TOKEN_DIV_ASSIGN);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_DIV);
        NextChar();
      }
      break;

    case L'%':
      tokens[index]->SetType(TOKEN_MOD);
      NextChar();
      break;
        
      // L'≠':
    case 0x2260:
      tokens[index]->SetType(TOKEN_NEQL);
      NextChar();
      break;

      // L'←':
    case 0x2190:
      tokens[index]->SetType(TOKEN_ASSIGN);
      NextChar();
      break;

    // L'→':
    case 0x2192:
      tokens[index]->SetType(TOKEN_ASSESSOR);
      NextChar();
      break;

      // L'≤':
    case 0x2264:
      tokens[index]->SetType(TOKEN_LEQL);
      NextChar();
      break;

      // L'≥':
    case 0x2265:
      tokens[index]->SetType(TOKEN_GEQL);
      NextChar();
      break;

    case EOB:
    case 0xfffd:
      tokens[index]->SetType(TOKEN_END_OF_STREAM);
      break;

    default:
      tokens[index]->SetType(TOKEN_UNKNOWN);
      NextChar();
      break;
    }
    return;
  }
}
