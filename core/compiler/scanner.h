/***************************************************************************
 * Language scanner.
 *
 * Copyright (c) 2025, Randy Hollines
 * All rights reserved.
 *
 * Reistribution and use in source and binary forms, with or without
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

#pragma once

#include "tree.h"
#ifdef _WIN32
#include <windows.h>
#endif

// comment
#define COMMENT L'#'
#define ALT_COMMENT L'/'
#define EXTENDED_COMMENT L'~'
#define ALT_EXTENDED_COMMENT L'*'

// look ahead value
#define LOOK_AHEAD 3
// white space
#define WHITE_SPACE (iswspace(cur_char) || cur_char == 0x200b || cur_char == 0xfeff)

/****************************
 * Token types
 ****************************/
enum ScannerTokenType {
  // misc
  TOKEN_END_OF_STREAM = -1000,
  TOKEN_NO_INPUT,
  TOKEN_UNKNOWN,
  // symbols
  TOKEN_TILDE,
  TOKEN_BACK_SLASH,
  TOKEN_PERIOD,
  TOKEN_COLON,
  TOKEN_SEMI_COLON,
  TOKEN_HAT,
  TOKEN_COMMA,
  TOKEN_ASSIGN,
  TOKEN_ADD_ADD,
  TOKEN_SUB_SUB,
  TOKEN_ADD_ASSIGN,
  TOKEN_SUB_ASSIGN,
  TOKEN_MUL_ASSIGN,
  TOKEN_DIV_ASSIGN,
  TOKEN_OPEN_BRACE,
  TOKEN_CLOSED_BRACE,
  TOKEN_OPEN_PAREN,
  TOKEN_CLOSED_PAREN,
  TOKEN_OPEN_BRACKET,
  TOKEN_CLOSED_BRACKET,
  TOKEN_ASSESSOR,
  TOKEN_LAMBDA,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_QUESTION,
  TOKEN_NOT,
  // --- start logic ---
  TOKEN_LES,
  TOKEN_GTR,
  TOKEN_LEQL,
  TOKEN_GEQL,
  TOKEN_EQL,
  TOKEN_NEQL,
  // --- end logic ---
  TOKEN_ADD,
  TOKEN_SUB,
  // --- start factor ---
  TOKEN_MUL,
  TOKEN_DIV,
  TOKEN_MOD,
  TOKEN_SHL,
  TOKEN_SHR,
  TOKEN_AND_ID,
  TOKEN_OR_ID,
  TOKEN_XOR_ID,
  TOKEN_NOT_ID,
  // --- end: factor ---
  TOKEN_IDENT,
  // literals
  TOKEN_INT_LIT,
  TOKEN_FLOAT_LIT,
  TOKEN_CHAR_LIT,
  TOKEN_CHAR_STRING_LIT,
  TOKEN_BAD_CHAR_STRING_LIT,
  // reserved words
  TOKEN_NATIVE_ID,
  TOKEN_IF_ID,
  TOKEN_ELSE_ID,
  TOKEN_DO_ID,
  TOKEN_WHILE_ID,
  TOKEN_BREAK_ID,
  TOKEN_CONTINUE_ID,
  TOKEN_BOOLEAN_ID,
  TOKEN_TRUE_ID,
  TOKEN_FALSE_ID,
  TOKEN_USE_ID,
  TOKEN_BUNDLE_ID,
  TOKEN_VIRTUAL_ID,
  TOKEN_STATIC_ID,
  TOKEN_PUBLIC_ID,
  TOKEN_PRIVATE_ID,
  TOKEN_RETURN_ID,
  TOKEN_LEAVING_ID,
  TOKEN_AS_ID,
  TOKEN_TYPE_OF_ID,
  TOKEN_PARENT_ID,
  TOKEN_FROM_ID,
  TOKEN_FOR_ID,
  TOKEN_EACH_ID,
  TOKEN_IN_ID,
  TOKEN_REVERSE_ID,
  TOKEN_ENUM_ID,
  TOKEN_CONSTS_ID,
  TOKEN_SELECT_ID,
  TOKEN_OTHER_ID,
  TOKEN_LABEL_ID,
  TOKEN_NEW_ID,
  TOKEN_CLASS_ID,
  TOKEN_INTERFACE_ID,
  TOKEN_ALIAS_ID,
  TOKEN_IMPLEMENTS_ID,
  TOKEN_FUNCTION_ID,
  TOKEN_METHOD_ID,
  TOKEN_BYTE_ID,
  TOKEN_INT_ID,
  TOKEN_FLOAT_ID,
  TOKEN_CHAR_ID,
  TOKEN_NIL_ID,
  TOKEN_CRITICAL_ID,
#ifdef _SYSTEM
  S2I,
  S2F,
  I2S,
  F2S,
  LOAD_ARY_SIZE,
  CPY_BYTE_ARY,
  CPY_CHAR_ARY,
  CPY_INT_ARY,
  CPY_FLOAT_ARY,
  ZERO_BYTE_ARY,
  ZERO_CHAR_ARY,
  ZERO_INT_ARY,
  ZERO_FLOAT_ARY,
  FLOR_FLOAT,
  CEIL_FLOAT,
  TRUNC_FLOAT,
  SIN_FLOAT,
  COS_FLOAT,
  TAN_FLOAT,
  ASIN_FLOAT,
  ACOS_FLOAT,
  ATAN_FLOAT,
  LOG2_FLOAT,
  CBRT_FLOAT,
  ATAN2_FLOAT,
  ACOSH_FLOAT,
  ASINH_FLOAT,
  ATANH_FLOAT,
  COSH_FLOAT,
  SINH_FLOAT,
  TANH_FLOAT,
  MOD_FLOAT,
  LOG_FLOAT,
  ROUND_FLOAT,
  EXP_FLOAT,
  LOG10_FLOAT,
  POW_FLOAT,
  SQRT_FLOAT,
  GAMMA_FLOAT,
  RAND_FLOAT,
  LOAD_CLS_INST_ID,
  STRING_HASH_ID,
  LOAD_CLS_BY_INST,
  LOAD_NEW_OBJ_INST,
  LOAD_INST_UID,
  LOAD_MULTI_ARY_SIZE,
  // limits
  NAN_INT,
  INF_INT,
  NEG_INF_INT,
  NAN_FLOAT,
  INF_FLOAT,
  NEG_INF_FLOAT,
  // standard i/o
  STD_OUT_BOOL,
  STD_OUT_BYTE,
  STD_OUT_CHAR,
  STD_OUT_INT,
  STD_OUT_FLOAT,
	STD_INT_FMT,
	STD_FLOAT_FMT,
	STD_FLOAT_PER,
	STD_WIDTH,
	STD_FILL,
  STD_OUT_STRING,
  STD_IN_BYTE_ARY_LEN,
  STD_OUT_BYTE_ARY_LEN,
  STD_IN_CHAR_ARY_LEN,
  STD_OUT_CHAR_ARY_LEN,
  STD_IN_CHAR_ARY,
  STD_IN_STRING,
  STD_FLUSH,
  // standard error i/o
  STD_ERR_BOOL,
  STD_ERR_BYTE,
  STD_ERR_CHAR,
  STD_ERR_INT,
  STD_ERR_FLOAT,
  STD_ERR_STRING,
  STD_ERR_CHAR_ARY,
  STD_ERR_BYTE_ARY,
  STD_ERR_FLUSH,
  CRC32_BYTES,
  // compress
  COMPRESS_ZLIB_BYTES,
  UNCOMPRESS_ZLIB_BYTES,
  COMPRESS_GZIP_BYTES,
  UNCOMPRESS_GZIP_BYTES,
  COMPRESS_BR_BYTES,
  UNCOMPRESS_BR_BYTES,
  // file open/close
  FILE_OPEN_READ,
  FILE_OPEN_APPEND,
  FILE_OPEN_WRITE,
  FILE_OPEN_READ_WRITE,
  FILE_CLOSE,
  FILE_FLUSH,
  // file-in
  FILE_IN_BYTE,
  FILE_IN_BYTE_ARY,
  FILE_IN_CHAR_ARY,
  FILE_IN_STRING,
  // file-out
  FILE_OUT_BYTE,
  FILE_OUT_BYTE_ARY,
  FILE_OUT_CHAR_ARY,
  FILE_OUT_STRING,
  // file-operations
  FILE_EXISTS,
  FILE_CAN_READ_ONLY,
  FILE_CAN_WRITE_ONLY,
  FILE_CAN_READ_WRITE,
  FILE_IS_OPEN,
  FILE_SIZE,
  FILE_FULL_PATH,
  FILE_TEMP_NAME,
  FILE_SEEK,
  FILE_REWIND,
  FILE_EOF,
  FILE_DELETE,
  FILE_RENAME,
  FILE_COPY,
  // named pipe
  PIPE_OPEN,
  PIPE_CREATE,
  PIPE_IN_BYTE,
  PIPE_OUT_BYTE,
  PIPE_IN_BYTE_ARY,
  PIPE_IN_CHAR_ARY,
  PIPE_OUT_BYTE_ARY,
  PIPE_OUT_CHAR_ARY,
  PIPE_IN_STRING,
  PIPE_OUT_STRING,
  PIPE_CLOSE,
  // directory operations
  DIR_CREATE,
  DIR_SLASH,
  DIR_EXISTS,
  DIR_LIST,
  DIR_DELETE,
  DIR_COPY,
  DIR_GET_CUR,
  DIR_SET_CUR,
  // symbolic link operations
  SYM_LINK_CREATE,
  SYM_LINK_COPY,
  SYM_LINK_LOC,
  SYM_LINK_EXISTS,
  // hard link operations
  HARD_LINK_CREATE,
  // socket operations
  SOCK_TCP_CONNECT,
  SOCK_TCP_IS_CONNECTED,
  SOCK_TCP_CLOSE,
  // TCP socket server operations
  SOCK_TCP_BIND,
  SOCK_TCP_LISTEN,
  SOCK_TCP_ACCEPT,
  SOCK_TCP_SELECT,
  SOCK_IP_ERROR,
  // UDP socket server operations
  SOCK_UDP_CREATE,
  SOCK_UDP_BIND,
  SOCK_UDP_CLOSE,
  SOCK_UDP_IN_BYTE,
  SOCK_UDP_IN_BYTE_ARY,
  SOCK_UDP_IN_CHAR_ARY,
  SOCK_UDP_OUT_BYTE,
  SOCK_UDP_OUT_BYTE_ARY,
  SOCK_UDP_OUT_CHAR_ARY,
  SOCK_UDP_IN_STRING,
  SOCK_UDP_OUT_STRING,
	// secure socket server operations
  SOCK_TCP_SSL_LISTEN,
  SOCK_TCP_SSL_ACCEPT,
  SOCK_TCP_SSL_SELECT,
  SOCK_TCP_SSL_SRV_CERT,
  SOCK_TCP_SSL_ERROR,
  SOCK_TCP_SSL_SRV_CLOSE,
  // socket-in
  SOCK_TCP_IN_BYTE,
  SOCK_TCP_IN_BYTE_ARY,
  SOCK_TCP_IN_CHAR_ARY,
  SOCK_TCP_IN_STRING,
  // socket-out
  SOCK_TCP_OUT_BYTE,
  SOCK_TCP_OUT_BYTE_ARY,
  SOCK_TCP_OUT_CHAR_ARY,
  SOCK_TCP_OUT_STRING,
  SOCK_TCP_HOST_NAME,
  SOCK_TCP_RESOLVE_NAME,
  // secure socket operations
  SOCK_TCP_SSL_CONNECT,
  SOCK_TCP_SSL_ISSUER,
  SOCK_TCP_SSL_SUBJECT,
  SOCK_TCP_SSL_CLOSE,  
  // secure socket-in
  SOCK_TCP_SSL_IN_BYTE,
  SOCK_TCP_SSL_IN_BYTE_ARY,
  SOCK_TCP_SSL_IN_CHAR_ARY,
  SOCK_TCP_SSL_IN_STRING,
  // secure socket-out
  SOCK_TCP_SSL_OUT_BYTE,
  SOCK_TCP_SSL_OUT_BYTE_ARY,
  SOCK_TCP_SSL_OUT_CHAR_ARY,
  SOCK_TCP_SSL_OUT_STRING,
  // serialization
  SERL_CHAR,
  SERL_INT,
  SERL_FLOAT,
  SERL_OBJ_INST,
  SERL_BYTE_ARY,
  SERL_CHAR_ARY,
  SERL_INT_ARY,
  SERL_OBJ_ARY,
  SERL_FLOAT_ARY,
  DESERL_INT,
  DESERL_CHAR,
  DESERL_FLOAT,
  DESERL_OBJ_INST,
  DESERL_BYTE_ARY,
  DESERL_CHAR_ARY,
  DESERL_INT_ARY,
  DESERL_OBJ_ARY,
  DESERL_FLOAT_ARY,
  // shared library support
  EXT_LIB_LOAD,
  EXT_LIB_UNLOAD,
  EXT_LIB_FUNC_CALL,
  // thread management
  ASYNC_MTHD_CALL,
  THREAD_MUTEX,
  THREAD_SLEEP,
  THREAD_JOIN,
  // strings
  BYTES_TO_UNICODE,
  UNICODE_TO_BYTES,
  // time
  SYS_TIME,
  GMT_TIME,
  FILE_CREATE_TIME,
  FILE_MODIFIED_TIME,
  FILE_ACCESSED_TIME,
  FILE_ACCOUNT_OWNER,
  FILE_GROUP_OWNER,
  DATE_TIME_SET_1,
  DATE_TIME_SET_2,
  DATE_TIME_ADD_YEARS,
  DATE_TIME_ADD_DAYS,
  DATE_TIME_ADD_HOURS,
  DATE_TIME_ADD_MINS,
  DATE_TIME_ADD_SECS,
  DATE_TO_UNIX_TIME,
  DATE_FROM_UNIX_GMT_TIME,
  DATE_FROM_UNIX_LOCAL_TIME,
  TIMER_START,
  TIMER_END,
  TIMER_ELAPSED,
  // platform
  GET_PLTFRM,
  GET_VERSION,
  GET_SYS_PROP,
  SET_SYS_PROP,
  GET_SYS_ENV,
  SET_SYS_ENV,
  ASSERT_TRUE,
  SYS_CMD,
  SYS_CMD_OUT,
  SET_SIGNAL,
  RAISE_SIGNAL,
  EXIT
#endif
};

/****************************
 * Token class
 ****************************/
class Token {
 private:
  ScannerTokenType token_type;
  int line_nbr;
  int line_pos;
  std::wstring filename;
  std::wstring ident;

  union {
    INT64_VALUE int64_lit;
    FLOAT_VALUE double_lit;
    wchar_t char_lit;
    char byte_lit;
  } kind;

 public:
   Token() {

   }

  inline void Copy(Token* token) {
    line_nbr = token->line_nbr;
    line_pos = token->line_pos;
    kind.char_lit = token->kind.char_lit;
    kind.int64_lit = token->kind.int64_lit;
    kind.double_lit = token->kind.double_lit;
    ident = token->ident;
    token_type = token->token_type;
    filename = token->filename;
  }

  inline const std::wstring GetFileName() {
    return filename;
  }

  inline void SetFileName(std::wstring f) {
    filename = f;
  }

  inline const int GetLineNumber() {
    return line_nbr;
  }

  inline const int GetLinePosition() {
    return line_pos;
  }

  inline void SetLineNbr(int l) {
    line_nbr = l;
  }

  inline void SetLinePos(int p) {
    line_pos = p;
  }

  inline void  SetInt64Lit(INT64_VALUE i) {
    kind.int64_lit = i;
  }

  inline void SetFloatLit(FLOAT_VALUE d) {
    kind.double_lit = d;
  }

  inline void SetByteLit(char b) {
    kind.byte_lit = b;
  }

  inline void SetCharLit(wchar_t c) {
    kind.char_lit = c;
  }

  inline void SetIdentifier(std::wstring i) {
    ident = i;
  }

  inline const INT64_VALUE GetInt64Lit() {
    return kind.int64_lit;
  }

  inline const FLOAT_VALUE GetFloatLit() {
    return kind.double_lit;
  }

  inline const char GetByteLit() {
    return kind.byte_lit;
  }

  inline const wchar_t GetCharLit() {
    return kind.char_lit;
  }

  inline const std::wstring GetIdentifier() {
    return ident;
  }

  inline const ScannerTokenType GetType() {
    return token_type;
  }

  inline void SetType(ScannerTokenType t) {
    token_type = t;
  }
};

/**********************************
 * Token scanner with k lookahead
 * tokens
 **********************************/
class Scanner {
 private:
  // syntax
  bool alt_syntax;
  // input file name
  std::wstring filename;
  // input buffer
  wchar_t* buffer;
  // buffer size
  size_t buffer_size;
  // input buffer position
  size_t buffer_pos;
  bool is_first_token;
  // start marker position
  size_t start_pos;
  // end marker position
  size_t end_pos;
  // input characters
  wchar_t cur_char, nxt_char, nxt_nxt_char;
  // map of reserved identifiers
  std::map<const std::wstring, ScannerTokenType> ident_map;
  // array of tokens for lookahead
  Token* tokens[LOOK_AHEAD];
  // line number
  int line_nbr;
  size_t line_pos;
  
  // parsers a character string
  void CheckString(int index, bool is_lit, bool is_valid);
  // parse an integer
  void ParseInteger(int index, int base = 0);
  // parse a double
  void ParseDouble(int index);
  // parsers an Unicode character
  void ParseUnicodeChar(int index);
  // read input file into memory
  void ReadFile();
  // ignore white space
  void Whitespace();
  // ignore comments
  void Comments();
  // next character
  void NextChar();
  // load reserved keywords
  void LoadKeywords();
  // parses a new token
  void ParseToken(int index);
  // check identifier
  void CheckIdentifier(int index);
  // create a random string
  std::wstring RandomString(size_t len);

 public:
  // default constructor
  Scanner(std::wstring f, bool a, const std::wstring c = L"");
  // default destructor
  ~Scanner();

  // next token
  void NextToken();

  // token accessor
  Token* GetToken(int index = 0);

  // gets the file name
  std::wstring GetFileName() {
    return filename;
  }
};
