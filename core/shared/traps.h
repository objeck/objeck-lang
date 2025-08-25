/***************************************************************************
 *
 *
 * Copyright (c) 2008-2011, Randy Hollines
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

#pragma once

namespace instructions {
  // system directive codes
  enum Traps {
    // core operations
    LOAD_CLS_INST_ID = -4000,
    LOAD_CLS_BY_INST,
    LOAD_CLS_BY_NAME,
    LOAD_NEW_OBJ_INST,
    LOAD_INST_UID,
    LOAD_MULTI_ARY_SIZE,
    CPY_CHAR_STR_ARY,
    CPY_CHAR_STR_ARYS,
    CPY_INT_STR_ARY,
    CPY_BYTE_STR_ARY,
    CPY_BOOL_STR_ARY,
    CPY_FLOAT_STR_ARY,
    BYTES_TO_UNICODE,
    UNICODE_TO_BYTES,
    STRING_HASH_ID,
    // time
    SYS_TIME,
    GMT_TIME,
    FILE_CREATE_TIME,
    FILE_MODIFIED_TIME,
    FILE_ACCESSED_TIME,
    FILE_LOGGER_DIR,
    FILE_ACCOUNT_OWNER,
    FILE_GROUP_OWNER,
    DATE_TIME_SET_1,
    DATE_TIME_SET_2,
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
    // standard i/o
    STD_IN_STRING,
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
    STD_OUT_BYTE_ARY_LEN,
    STD_OUT_CHAR_ARY_LEN,
    STD_IN_BYTE_ARY_LEN,
    STD_IN_CHAR_ARY_LEN,
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
    // compression
    COMPRESS_ZLIB_BYTES,
    UNCOMPRESS_ZLIB_BYTES,
    COMPRESS_GZIP_BYTES,
    UNCOMPRESS_GZIP_BYTES,
    COMPRESS_BR_BYTES,
    UNCOMPRESS_BR_BYTES,
    CRC32_BYTES,
    // file i/o
    FILE_OPEN_READ,
    FILE_OPEN_APPEND,
    FILE_OPEN_WRITE,
    FILE_OPEN_READ_WRITE,
    FILE_CLOSE,
    FILE_FLUSH,
    FILE_IN_BYTE,
    FILE_OUT_BYTE,
    FILE_IN_BYTE_ARY,
    FILE_IN_CHAR_ARY,
    FILE_OUT_BYTE_ARY,
    FILE_OUT_CHAR_ARY,
    FILE_IN_STRING,
    FILE_OUT_STRING,
    FILE_IS_OPEN,
    FILE_EXISTS,
    FILE_CAN_WRITE_ONLY,
    FILE_CAN_READ_ONLY,
    FILE_CAN_READ_WRITE,
    FILE_SIZE,
    FILE_FULL_PATH,
    FILE_TEMP_NAME,
    FILE_REWIND,
    FILE_SEEK,
    FILE_EOF,
    FILE_DELETE,
    FILE_RENAME,
    FILE_COPY,
    // pipe i/o
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
    DIR_COPY,
    DIR_GET_CUR,
    DIR_SET_CUR,
    DIR_DELETE,
    // symbolic link operations
    SYM_LINK_CREATE,
    SYM_LINK_COPY,
    SYM_LINK_LOC,
    SYM_LINK_EXISTS,
    // hard link operations
    HARD_LINK_CREATE,
    // TCP socket i/o
    SOCK_TCP_CONNECT,
    SOCK_TCP_BIND,
    SOCK_TCP_LISTEN,
    SOCK_TCP_ACCEPT,
    SOCK_TCP_SELECT,
    SOCK_TCP_IS_CONNECTED,
    SOCK_TCP_CLOSE,
    SOCK_TCP_IN_BYTE,
    SOCK_TCP_IN_BYTE_ARY,
    SOCK_TCP_IN_CHAR_ARY,
    SOCK_TCP_OUT_BYTE,
    SOCK_TCP_OUT_BYTE_ARY,
    SOCK_TCP_OUT_CHAR_ARY,
    SOCK_TCP_IN_STRING,
    SOCK_TCP_OUT_STRING,
    SOCK_TCP_HOST_NAME,
    SOCK_TCP_RESOLVE_NAME,
    SOCK_IP_ERROR,
    // UDP socket i/o
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
    // ssl socket
    SOCK_TCP_SSL_CONNECT,
    SOCK_TCP_SSL_CLOSE,
    SOCK_TCP_SSL_ISSUER,
    SOCK_TCP_SSL_SUBJECT,
    SOCK_TCP_SSL_IN_BYTE,
    SOCK_TCP_SSL_IN_BYTE_ARY,
    SOCK_TCP_SSL_IN_CHAR_ARY,
    SOCK_TCP_SSL_OUT_BYTE,
    SOCK_TCP_SSL_OUT_BYTE_ARY,
    SOCK_TCP_SSL_OUT_CHAR_ARY,
    SOCK_TCP_SSL_IN_STRING,
    SOCK_TCP_SSL_OUT_STRING,
    SOCK_TCP_SSL_LISTEN,
    SOCK_TCP_SSL_ACCEPT,
    SOCK_TCP_SSL_SELECT,
    SOCK_TCP_SSL_SRV_CERT,
    SOCK_TCP_SSL_ERROR,
		SOCK_TCP_SSL_SRV_CLOSE,
    // serialization
    SERL_INT,
    SERL_FLOAT,
    SERL_OBJ_INST,
    SERL_BYTE_ARY,
    SERL_INT_ARY,
    SERL_OBJ_ARY,
    SERL_FLOAT_ARY,
    SERL_CHAR,
    SERL_CHAR_ARY,
    DESERL_INT,
    DESERL_FLOAT,
    DESERL_OBJ_INST,
    DESERL_BYTE_ARY,
    DESERL_INT_ARY,
    DESERL_OBJ_ARY,
    DESERL_FLOAT_ARY,
    DESERL_CHAR,
    DESERL_CHAR_ARY,
    // platform
    GET_PLTFRM,
    GET_VERSION,
    GET_SYS_PROP,
    SET_SYS_PROP,
    GET_SYS_ENV,
    SET_SYS_ENV,
    SYS_CMD,
    SYS_CMD_OUT,
    ASSERT_TRUE,
    // end
    EXIT
  };
}
