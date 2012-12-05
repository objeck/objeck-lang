/**************************************************************************
 * Language scanner.
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

#include "scanner.h"

#define EOB '\0'

/****************************
* Scanner constructor
****************************/
Scanner::Scanner(string f, bool p)
{
  // copy file name
  filename = f;
  cur_char = '\0';
  // create tokens
  for(int i = 0; i < LOOK_AHEAD; i++) {
    tokens[i] = new Token;
  }
  // load identifiers into map
  LoadKeywords();
  // read file into memory
  if(p) {
    buffer_pos = 0;
	is_first_token = true;
    buffer_size = f.size();
    buffer = (char*)calloc(buffer_size, sizeof(char));
    strcpy(buffer, f.c_str());
#ifdef _DEBUG
  cout << "---------- Source (inline) ---------" << endl;
  cout << buffer << endl;
#endif
  }
  else {
    ReadFile();
  }
  // set line number to 1
  line_nbr = 1;
}

/****************************
* Scanner destructor
****************************/
Scanner::~Scanner()
{
  // delete buffer
  if(buffer) {
    free(buffer);
    buffer = NULL;
  }
  // delete token array
  for(int i = 0; i < LOOK_AHEAD; i++) {
    delete tokens[i];
    tokens[i] = NULL;
  }
}

/****************************
* Loads language keywords
****************************/
void Scanner::LoadKeywords()
{
  ident_map["and"] = TOKEN_AND_ID,
  ident_map["or"] = TOKEN_OR_ID,
  ident_map["xor"] = TOKEN_XOR_ID,
  ident_map["virtual"] = TOKEN_VIRTUAL_ID;
  ident_map["if"] = TOKEN_IF_ID;
  ident_map["else"] = TOKEN_ELSE_ID;
  ident_map["do"] = TOKEN_DO_ID;
  ident_map["while"] = TOKEN_WHILE_ID;
  ident_map["break"] = TOKEN_BREAK_ID;
  ident_map["use"] = TOKEN_USE_ID;
  ident_map["bundle"] = TOKEN_BUNDLE_ID;
  ident_map["native"] = TOKEN_NATIVE_ID;
  ident_map["static"] = TOKEN_STATIC_ID;
  ident_map["public"] = TOKEN_PUBLIC_ID;
  ident_map["private"] = TOKEN_PRIVATE_ID;
  ident_map["class"] = TOKEN_CLASS_ID;
  ident_map["interface"] = TOKEN_INTERFACE_ID;
  ident_map["implements"] = TOKEN_IMPLEMENTS_ID;
  ident_map["function"] = TOKEN_FUNCTION_ID;
  ident_map["method"] = TOKEN_METHOD_ID;
  ident_map["select"] = TOKEN_SELECT_ID;
  ident_map["other"] = TOKEN_OTHER_ID;
  ident_map["enum"] = TOKEN_ENUM_ID;
  ident_map["for"] = TOKEN_FOR_ID;
  ident_map["each"] = TOKEN_EACH_ID;
  ident_map["label"] = TOKEN_LABEL_ID;
  ident_map["return"] = TOKEN_RETURN_ID;
  ident_map["Byte"] = TOKEN_BYTE_ID;
  ident_map["Int"] = TOKEN_INT_ID;
  ident_map["Parent"] = TOKEN_PARENT_ID;
  ident_map["from"] = TOKEN_FROM_ID;
  ident_map["Float"] = TOKEN_FLOAT_ID;
  ident_map["Char"] = TOKEN_CHAR_ID;
  ident_map["Bool"] = TOKEN_BOOLEAN_ID;
  ident_map["true"] = TOKEN_TRUE_ID;
  ident_map["false"] = TOKEN_FALSE_ID;
  ident_map["New"] = TOKEN_NEW_ID;
  ident_map["Nil"] = TOKEN_NIL_ID;
  ident_map["As"] = TOKEN_AS_ID;
  ident_map["TypeOf"] = TOKEN_TYPE_OF_ID;
  ident_map["critical"] = TOKEN_CRITICAL_ID;
#ifdef _SYSTEM
  ident_map["CPY_BYTE_ARY"] = CPY_BYTE_ARY;
  ident_map["CPY_INT_ARY"] = CPY_INT_ARY;
  ident_map["CPY_FLOAT_ARY"] = CPY_FLOAT_ARY;
  ident_map["FLOR_FLOAT"] = FLOR_FLOAT;
  ident_map["CEIL_FLOAT"] = CEIL_FLOAT;
  ident_map["SIN_FLOAT"] = SIN_FLOAT;
  ident_map["COS_FLOAT"] = COS_FLOAT;
  ident_map["TAN_FLOAT"] = TAN_FLOAT;
  ident_map["ASIN_FLOAT"] = ASIN_FLOAT;
  ident_map["ACOS_FLOAT"] = ACOS_FLOAT;
  ident_map["ATAN_FLOAT"] = ATAN_FLOAT;
  ident_map["LOG_FLOAT"] = LOG_FLOAT;
  ident_map["POW_FLOAT"] = POW_FLOAT;
  ident_map["SQRT_FLOAT"] = SQRT_FLOAT;
  ident_map["RAND_FLOAT"] = RAND_FLOAT;
  ident_map["LOAD_CLS_INST_ID"] = LOAD_CLS_INST_ID;
  ident_map["LOAD_CLS_BY_INST"] = LOAD_CLS_BY_INST;
  ident_map["LOAD_NEW_OBJ_INST"] = LOAD_NEW_OBJ_INST;
  ident_map["LOAD_INST_UID"] = LOAD_INST_UID;
  ident_map["LOAD_ARY_SIZE"] = LOAD_ARY_SIZE;
  ident_map["LOAD_MULTI_ARY_SIZE"] = LOAD_MULTI_ARY_SIZE;
  // standard i/o
  ident_map["STD_IN_STRING"] = STD_IN_STRING;
  ident_map["STD_OUT_BOOL"] = STD_OUT_BOOL;
  ident_map["STD_OUT_BYTE"] = STD_OUT_BYTE;
  ident_map["STD_OUT_CHAR"] = STD_OUT_CHAR;
  ident_map["STD_OUT_INT"] = STD_OUT_INT;
  ident_map["STD_OUT_FLOAT"] = STD_OUT_FLOAT;
  ident_map["STD_OUT_CHAR_ARY"] = STD_OUT_CHAR_ARY;
  ident_map["STD_OUT_BYTE_ARY"] = STD_OUT_BYTE_ARY;
  // standard error i/o
  ident_map["STD_ERR_BOOL"] = STD_ERR_BOOL;
  ident_map["STD_ERR_BYTE"] = STD_ERR_BYTE;
  ident_map["STD_ERR_CHAR"] = STD_ERR_CHAR;
  ident_map["STD_ERR_INT"] = STD_ERR_INT;
  ident_map["STD_ERR_FLOAT"] = STD_ERR_FLOAT;
  ident_map["STD_ERR_CHAR_ARY"] = STD_ERR_CHAR_ARY;
  ident_map["STD_ERR_BYTE_ARY"] = STD_ERR_BYTE_ARY;

  // file i/o
  ident_map["FILE_OPEN_READ"] = FILE_OPEN_READ;
  ident_map["FILE_CLOSE"] = FILE_CLOSE;
  ident_map["FILE_FLUSH"] = FILE_FLUSH;
  ident_map["FILE_IN_BYTE"] = FILE_IN_BYTE;
  ident_map["FILE_IN_BYTE_ARY"] = FILE_IN_BYTE_ARY;
  ident_map["FILE_IN_STRING"] = FILE_IN_STRING;
  ident_map["FILE_OUT_BYTE"] = FILE_OUT_BYTE;
  ident_map["FILE_OUT_BYTE_ARY"] = FILE_OUT_BYTE_ARY;
  ident_map["FILE_OPEN_WRITE"] = FILE_OPEN_WRITE;
  ident_map["FILE_OPEN_READ_WRITE"] = FILE_OPEN_READ_WRITE;
  ident_map["FILE_OUT_BYTE"] = FILE_OUT_BYTE;
  ident_map["FILE_OUT_BYTE_ARY"] = FILE_OUT_BYTE_ARY;
  ident_map["FILE_OUT_STRING"] = FILE_OUT_STRING;
  ident_map["FILE_EXISTS"] = FILE_EXISTS;
  ident_map["FILE_SIZE"] = FILE_SIZE;
  ident_map["FILE_SEEK"] = FILE_SEEK;
  ident_map["FILE_EOF"] = FILE_EOF;
  ident_map["FILE_REWIND"] = FILE_REWIND;
  ident_map["FILE_IS_OPEN"] = FILE_IS_OPEN;
  ident_map["FILE_DELETE"] = FILE_DELETE;
  ident_map["FILE_RENAME"] = FILE_RENAME;
  ident_map["DIR_CREATE"] = DIR_CREATE;
  ident_map["DIR_EXISTS"] = DIR_EXISTS;
  ident_map["DIR_LIST"] = DIR_LIST;
  ident_map["ASYNC_MTHD_CALL"] = ASYNC_MTHD_CALL;
  ident_map["DLL_LOAD"] = DLL_LOAD;
  ident_map["DLL_UNLOAD"] = DLL_UNLOAD;
  ident_map["DLL_FUNC_CALL"] = DLL_FUNC_CALL;
  ident_map["THREAD_MUTEX"] = THREAD_MUTEX;
  ident_map["THREAD_SLEEP"] = THREAD_SLEEP;
  ident_map["THREAD_JOIN"] = THREAD_JOIN;
  ident_map["SYS_TIME"] = SYS_TIME;
  ident_map["GMT_TIME"] = GMT_TIME;
  ident_map["FILE_CREATE_TIME"] = FILE_CREATE_TIME;
  ident_map["FILE_MODIFIED_TIME"] = FILE_MODIFIED_TIME;
  ident_map["FILE_ACCESSED_TIME"] = FILE_ACCESSED_TIME;
  ident_map["DATE_TIME_SET_1"] = DATE_TIME_SET_1;
  ident_map["DATE_TIME_SET_2"] = DATE_TIME_SET_2;
  ident_map["DATE_TIME_SET_3"] = DATE_TIME_SET_3;
  ident_map["DATE_TIME_ADD_DAYS"] = DATE_TIME_ADD_DAYS;
  ident_map["DATE_TIME_ADD_HOURS"] = DATE_TIME_ADD_HOURS;
  ident_map["DATE_TIME_ADD_MINS"] = DATE_TIME_ADD_MINS;
  ident_map["DATE_TIME_ADD_SECS"] = DATE_TIME_ADD_SECS; 
  ident_map["PLTFRM"] = PLTFRM;
  ident_map["GET_SYS_PROP"] = GET_SYS_PROP;
  ident_map["SET_SYS_PROP"] = SET_SYS_PROP;
  ident_map["EXIT"] = EXIT;
  ident_map["TIMER_START"] = TIMER_START;
  ident_map["TIMER_END"] =  TIMER_END;
  ident_map["SOCK_TCP_CONNECT"] = SOCK_TCP_CONNECT;
  ident_map["SOCK_TCP_IS_CONNECTED"] = SOCK_TCP_IS_CONNECTED;
  ident_map["SOCK_TCP_CLOSE"] = SOCK_TCP_CLOSE;
  ident_map["SOCK_TCP_IN_BYTE"] = SOCK_TCP_IN_BYTE;
  ident_map["SOCK_TCP_IN_BYTE_ARY"] = SOCK_TCP_IN_BYTE_ARY;
  ident_map["SOCK_TCP_OUT_STRING"] = SOCK_TCP_OUT_STRING;
  ident_map["SOCK_TCP_IN_STRING"] = SOCK_TCP_IN_STRING;
  ident_map["SOCK_TCP_OUT_BYTE"] = SOCK_TCP_OUT_BYTE;
  ident_map["SOCK_TCP_OUT_BYTE_ARY"] = SOCK_TCP_OUT_BYTE_ARY;
  ident_map["SOCK_TCP_HOST_NAME"] = SOCK_TCP_HOST_NAME;
  ident_map["SOCK_TCP_BIND"] = SOCK_TCP_BIND;
  ident_map["SOCK_TCP_LISTEN"] = SOCK_TCP_LISTEN;
  ident_map["SOCK_TCP_ACCEPT"] = SOCK_TCP_ACCEPT;
  ident_map["SERL_INT"] = SERL_INT;
  ident_map["SERL_FLOAT"] = SERL_FLOAT;
  ident_map["SERL_OBJ_INST"] = SERL_OBJ_INST;
  ident_map["SERL_BYTE_ARY"] = SERL_BYTE_ARY;
  ident_map["SERL_INT_ARY"] = SERL_INT_ARY;
  ident_map["SERL_FLOAT_ARY"] = SERL_FLOAT_ARY;
  ident_map["DESERL_INT"] = DESERL_INT;
  ident_map["DESERL_FLOAT"] = DESERL_FLOAT;
  ident_map["DESERL_OBJ_INST"] = DESERL_OBJ_INST;
  ident_map["DESERL_BYTE_ARY"] = DESERL_BYTE_ARY;
  ident_map["DESERL_INT_ARY"] = DESERL_INT_ARY;
  ident_map["DESERL_FLOAT_ARY"] = DESERL_FLOAT_ARY;
#endif
}

/****************************
* Processes language
* identifies
****************************/
void Scanner::CheckIdentifier(int index)
{
  // copy string
  const int length = end_pos - start_pos;
  string ident(buffer, start_pos, length);
  // check string
  TokenType ident_type = ident_map[ident];
  switch(ident_type) {
  case TOKEN_AND_ID:
  case TOKEN_OR_ID:
  case TOKEN_XOR_ID:
  case TOKEN_CRITICAL_ID:
  case TOKEN_VIRTUAL_ID:
  case TOKEN_FROM_ID:
  case TOKEN_OTHER_ID:
  case TOKEN_ENUM_ID:
  case TOKEN_FOR_ID:
  case TOKEN_EACH_ID:
  case TOKEN_SELECT_ID:
  case TOKEN_LABEL_ID:
  case TOKEN_NATIVE_ID:
  case TOKEN_IF_ID:
  case TOKEN_ELSE_ID:
  case TOKEN_DO_ID:
  case TOKEN_WHILE_ID:
  case TOKEN_BREAK_ID:
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
  case TOKEN_IMPLEMENTS_ID:
  case TOKEN_FUNCTION_ID:
  case TOKEN_METHOD_ID:
  case TOKEN_BYTE_ID:
  case TOKEN_INT_ID:
  case TOKEN_RETURN_ID:
  case TOKEN_FLOAT_ID:
  case TOKEN_CHAR_ID:
  case TOKEN_NEW_ID:
  case TOKEN_NIL_ID:
#ifdef _SYSTEM
  case CPY_BYTE_ARY:
  case CPY_INT_ARY:
  case CPY_FLOAT_ARY:
  case FLOR_FLOAT:
  case CEIL_FLOAT:
  case SIN_FLOAT:
  case COS_FLOAT:
  case TAN_FLOAT:
  case ASIN_FLOAT:
  case ACOS_FLOAT:
  case ATAN_FLOAT:
  case LOG_FLOAT:
  case POW_FLOAT:
  case SQRT_FLOAT:
  case RAND_FLOAT:
  case LOAD_CLS_INST_ID:
  case LOAD_CLS_BY_INST:
  case LOAD_NEW_OBJ_INST:
  case LOAD_INST_UID:
  case LOAD_ARY_SIZE:
  case LOAD_MULTI_ARY_SIZE:
  case STD_IN_STRING:
  case STD_OUT_CHAR_ARY:
  case STD_OUT_BYTE_ARY:
  case STD_OUT_BOOL:
  case STD_OUT_BYTE:
  case STD_OUT_CHAR:
  case STD_OUT_INT:
  case STD_OUT_FLOAT:
  case STD_ERR_CHAR_ARY:
  case STD_ERR_BYTE_ARY:
  case STD_ERR_BOOL:
  case STD_ERR_BYTE:
  case STD_ERR_CHAR:
  case STD_ERR_INT:
  case STD_ERR_FLOAT:
  case FILE_OPEN_READ:
  case FILE_CLOSE:
  case FILE_FLUSH:
  case FILE_IN_BYTE:
  case FILE_IN_BYTE_ARY:
  case FILE_IN_STRING:
  case FILE_OPEN_WRITE:
  case FILE_OPEN_READ_WRITE:
  case FILE_OUT_BYTE:
  case FILE_OUT_BYTE_ARY:
  case FILE_OUT_STRING:
  case FILE_EXISTS:
  case FILE_SIZE:
  case FILE_SEEK:
  case FILE_EOF:
  case FILE_REWIND:
  case FILE_IS_OPEN:
  case FILE_DELETE:
  case FILE_RENAME:
  case DIR_CREATE:
  case DIR_EXISTS:
  case DIR_LIST:
  case ASYNC_MTHD_CALL:
  case DLL_LOAD:
  case DLL_UNLOAD:
  case DLL_FUNC_CALL:
  case THREAD_MUTEX:
  case THREAD_SLEEP:
  case THREAD_JOIN:
  case SYS_TIME:
  case GMT_TIME:
  case FILE_CREATE_TIME:
  case FILE_MODIFIED_TIME:
  case FILE_ACCESSED_TIME:
  case DATE_TIME_SET_1:
  case DATE_TIME_SET_2:
  case DATE_TIME_SET_3:
  case DATE_TIME_ADD_DAYS:
  case DATE_TIME_ADD_HOURS:
  case DATE_TIME_ADD_MINS:
  case DATE_TIME_ADD_SECS:
  case PLTFRM:
  case GET_SYS_PROP:
  case SET_SYS_PROP:
  case EXIT:
  case TIMER_START:
  case TIMER_END:
  case SOCK_TCP_CONNECT:
  case SOCK_TCP_IS_CONNECTED:
  case SOCK_TCP_CLOSE:
  case SOCK_TCP_IN_BYTE:
  case SOCK_TCP_IN_BYTE_ARY:
  case SOCK_TCP_IN_STRING:
  case SOCK_TCP_OUT_STRING:
  case SOCK_TCP_OUT_BYTE:
  case SOCK_TCP_OUT_BYTE_ARY:
  case SOCK_TCP_HOST_NAME:
  case SOCK_TCP_BIND:
  case SOCK_TCP_LISTEN:
  case SOCK_TCP_ACCEPT:
  case SERL_INT:
  case SERL_FLOAT:
  case SERL_OBJ_INST:
  case SERL_BYTE_ARY:
  case SERL_INT_ARY:
  case SERL_FLOAT_ARY:
  case DESERL_INT:
  case DESERL_FLOAT:
  case DESERL_OBJ_INST:
  case DESERL_BYTE_ARY:
  case DESERL_INT_ARY:
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
  tokens[index]->SetFileName(filename);
}

/****************************
* Reads a source input file
****************************/
void Scanner::ReadFile()
{
  buffer_pos = 0;
  is_first_token = true;
  buffer = LoadFileBuffer(filename, buffer_size);

  if(buffer_size > 2 && buffer[0] == -17 && buffer[1] == -69 && buffer[2] == -65) {
    buffer_pos = 3;
  }
#ifdef _DEBUG
  cout << "---------- Source ---------" << endl;
  cout << buffer << endl;
#endif
}

/****************************
* Processes the next token
****************************/
void Scanner::NextToken()
{
  if(is_first_token) {
    NextChar();
    for(int i = 0; i < LOOK_AHEAD; i++) {
      ParseToken(i);
    }
	is_first_token = false;
  } 
  else {
    int i = 1;
    for(; i < LOOK_AHEAD; i++) {
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
  
  return NULL;
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
    if(cur_char == '\n') {
      line_nbr++;
    }
    // current character
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
  while(cur_char == COMMENT && cur_char != EOB) {
    NextChar();
    // extended comment
    if(cur_char == EXTENDED_COMMENT) {
      NextChar();
      while(!(cur_char == EXTENDED_COMMENT && nxt_char == COMMENT) && cur_char != EOB) {
        NextChar();
      }
      NextChar();
      NextChar();
    }
    // line comment
    else {
      while(cur_char != '\n' && cur_char != EOB) {
        NextChar();
      }
    }
    Whitespace();
  }
  // character string
  if(cur_char == '\"') {
    NextChar();
    // mark
    start_pos = buffer_pos - 1;
    while(cur_char != '\"' && cur_char != EOB) {
      if(cur_char == '\\') {
        NextChar();
        switch(cur_char) {
        case '"':
          break;

        case '\\':
          break;

        case 'n':
          break;

        case 'r':
          break;

        case 't':
          break;

        case '0':
          break;

        default:
          tokens[index]->SetType(TOKEN_UNKNOWN);
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          break;
        }
      }
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    // check string
    NextChar();
    CheckString(index);
    return;
  }
  // character
  else if(cur_char == '\'') {
    NextChar();
    // escape or hex/unicode encoding
    if(cur_char == '\\') {
      NextChar();
      // read unicode string
      if(cur_char == 'u') {
        NextChar();
        start_pos = buffer_pos - 1;
        while(isdigit(cur_char) || (cur_char >= 'a' && cur_char <= 'f') ||
              (cur_char >= 'A' && cur_char <= 'F')) {
          NextChar();
        }
        end_pos = buffer_pos - 1;
        ParseUnicodeChar(index);
        if(cur_char != '\'') {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
        }
        NextChar();
        return;
      }
      // escape
      else if(nxt_char == '\'') {
        switch(cur_char) {
        case 'n':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\n');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	  
        case 'r':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\r');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	  
        case 't':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\t');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;

	case 'a':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\a');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	
	case 'b':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\b');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	
	case 'f':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\f');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;


        case '\\':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\\');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	  
	case '\'':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\'');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
	  
        case '0':
          tokens[index]->SetType(TOKEN_CHAR_LIT);
          tokens[index]->SetCharLit('\0');
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          NextChar();
          return;
        }
      }
      // error
      else {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
        return;
      }
    } else {
      // error
      if(nxt_char != '\'') {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
        return;
      } else {
        tokens[index]->SetType(TOKEN_CHAR_LIT);
        tokens[index]->SetCharLit(cur_char);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
        NextChar();
        return;
      }
    }
  }
  // identifier
#ifdef _SYSTEM
  else if(isalpha(cur_char) || cur_char == '$' || cur_char == '@') {
#else
  else if(isalpha(cur_char) || cur_char == '@') {
#endif
    // mark
    start_pos = buffer_pos - 1;

#ifdef _SYSTEM
    while((isalpha(cur_char) || isdigit(cur_char) || cur_char == '_' || cur_char == '@' || cur_char == '$') && cur_char != EOB) {
#else
    while((isalpha(cur_char) || isdigit(cur_char) || cur_char == '_' || cur_char == '@') && cur_char != EOB) {
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
  else if(isdigit(cur_char) || (cur_char == '.' && isdigit(nxt_char))) {
    int double_state = 0;
    int hex_state = 0;
    // mark
    start_pos = buffer_pos - 1;
    
    // test hex state
    if(cur_char == '0' && nxt_char == 'x') {
      hex_state = 1;
      NextChar();
    }
    while(isdigit(cur_char) || cur_char == '.' || 
	  // hex format
	  cur_char == 'x' || (cur_char >= 'a' && cur_char <= 'f') || 
	      (cur_char >= 'A' && cur_char <= 'F') ||
	  // scientific format
	  cur_char == 'e' || cur_char == 'E' || 
	  (double_state == 2 && (cur_char == '+' || cur_char == '-') && isdigit(nxt_char)))  {
      // decimal double
      if(cur_char == '.') {
        // error
        if(double_state || hex_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          break;
        }
        double_state = 1;
      }
      else if(cur_char == 'e' || cur_char == 'E') {
        // error
        if((double_state) != 1 || hex_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
          NextChar();
          break;
        }
        double_state = 2;
      }
      else if(double_state == 2 && (cur_char == '+' || cur_char == '-')) {
        double_state++;
      }      
      // hex integer
      else if(cur_char == 'x') {
	// error
        if(double_state) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          tokens[index]->SetLineNbr(line_nbr);
          tokens[index]->SetFileName(filename);
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
      
      // next character
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    if(double_state == 1 || double_state == 3) {
      ParseDouble(index);
    } 
    else if(hex_state == 2) {
      ParseInteger(index, 16);
    }
    else if(hex_state || double_state) {
      tokens[index]->SetType(TOKEN_UNKNOWN);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
    }
    else {
      ParseInteger(index);
    }
    return;
  }
  // other
  else {
    switch(cur_char) {
    case ':':
      if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_ASSIGN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else {
        tokens[index]->SetType(TOKEN_COLON);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);

        NextChar();
      }
      break;

    case '-':
      if(nxt_char == '>') {
        NextChar();
        tokens[index]->SetType(TOKEN_ASSESSOR);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_SUB_ASSIGN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else {
        tokens[index]->SetType(TOKEN_SUB);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);

        NextChar();
      }
      break;

    case '{':
      tokens[index]->SetType(TOKEN_OPEN_BRACE);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '.':
      tokens[index]->SetType(TOKEN_PERIOD);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '~':
      tokens[index]->SetType(TOKEN_TILDE);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '}':
      tokens[index]->SetType(TOKEN_CLOSED_BRACE);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '[':
      tokens[index]->SetType(TOKEN_OPEN_BRACKET);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case ']':
      tokens[index]->SetType(TOKEN_CLOSED_BRACKET);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '(':
      tokens[index]->SetType(TOKEN_OPEN_PAREN);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case ')':
      tokens[index]->SetType(TOKEN_CLOSED_PAREN);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case ',':
      tokens[index]->SetType(TOKEN_COMMA);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case ';':
      tokens[index]->SetType(TOKEN_SEMI_COLON);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '&':
      tokens[index]->SetType(TOKEN_AND);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '|':
      tokens[index]->SetType(TOKEN_OR);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '?':
      tokens[index]->SetType(TOKEN_QUESTION);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '=':
      tokens[index]->SetType(TOKEN_EQL);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case '<':
      if(nxt_char == '>') {
        NextChar();
        tokens[index]->SetType(TOKEN_NEQL);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_LEQL);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else if(nxt_char == '<') {
        NextChar();
        tokens[index]->SetType(TOKEN_SHL);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      } 
      else {
        tokens[index]->SetType(TOKEN_LES);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      break;

    case '>':
      if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_GEQL);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      else if(nxt_char == '>') {
        NextChar();
        tokens[index]->SetType(TOKEN_SHR);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      else {
        tokens[index]->SetType(TOKEN_GTR);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      break;

    case '+':
      if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_ADD_ASSIGN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      else {
	tokens[index]->SetType(TOKEN_ADD);
	tokens[index]->SetLineNbr(line_nbr);
	tokens[index]->SetFileName(filename);
	NextChar();
      }
      break;

    case '*':
      if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_MUL_ASSIGN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      else {
	tokens[index]->SetType(TOKEN_MUL);
	tokens[index]->SetLineNbr(line_nbr);
	tokens[index]->SetFileName(filename);
	NextChar();
      }
      break;

    case '/':
      if(nxt_char == '=') {
        NextChar();
        tokens[index]->SetType(TOKEN_DIV_ASSIGN);
        tokens[index]->SetLineNbr(line_nbr);
        tokens[index]->SetFileName(filename);
        NextChar();
      }
      else {
	tokens[index]->SetType(TOKEN_DIV);
	tokens[index]->SetLineNbr(line_nbr);
	tokens[index]->SetFileName(filename);
	NextChar();
      }
      break;

    case '%':
      tokens[index]->SetType(TOKEN_MOD);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;

    case EOB:
      tokens[index]->SetType(TOKEN_END_OF_STREAM);
      break;

    default:
      ProcessWarning();
      tokens[index]->SetType(TOKEN_UNKNOWN);
      tokens[index]->SetLineNbr(line_nbr);
      tokens[index]->SetFileName(filename);
      NextChar();
      break;
    }
    return;
  }
}
