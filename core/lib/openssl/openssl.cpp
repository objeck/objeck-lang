/***************************************************************************
 * Cryptographic support for Objeck (mbedTLS backend)
 *
 * Copyright (c) 2011-2026, Randy Hollines
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

#include <string.h>
#include <stdlib.h>
#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <mbedtls/base64.h>

#include "../../vm/lib_api.h"

// Digest length constants
#define SHA1_DIGEST_LEN    20
#define SHA256_DIGEST_LEN  32
#define SHA512_DIGEST_LEN  64
#define MD5_DIGEST_LEN     16
#define RIPEMD160_DIGEST_LEN 20
#define AES_BLOCK_LEN      16

//
// Reimplementation of OpenSSL's EVP_BytesToKey for AES backward compatibility.
// Derives key and IV from password + salt using iterated hashing.
//
static int evp_bytes_to_key(mbedtls_md_type_t md_type,
                            const unsigned char* salt, int salt_len,
                            const unsigned char* password, int password_len,
                            int count,
                            unsigned char* key_out, int key_len,
                            unsigned char* iv_out, int iv_len)
{
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(md_type);
  if(!md_info) {
    return 0;
  }

  const int md_size = mbedtls_md_get_size(md_info);
  unsigned char md_buf[MBEDTLS_MD_MAX_SIZE];
  const int total_needed = key_len + iv_len;
  int generated = 0;
  int addmd = 0;

  unsigned char* output = (unsigned char*)malloc(total_needed);
  if(!output) {
    return 0;
  }

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  if(mbedtls_md_setup(&ctx, md_info, 0) != 0) {
    mbedtls_md_free(&ctx);
    free(output);
    return 0;
  }

  while(generated < total_needed) {
    mbedtls_md_starts(&ctx);

    if(addmd) {
      mbedtls_md_update(&ctx, md_buf, md_size);
    }
    addmd = 1;

    mbedtls_md_update(&ctx, password, password_len);
    if(salt) {
      mbedtls_md_update(&ctx, salt, salt_len);
    }
    mbedtls_md_finish(&ctx, md_buf);

    for(int i = 1; i < count; i++) {
      mbedtls_md_starts(&ctx);
      mbedtls_md_update(&ctx, md_buf, md_size);
      mbedtls_md_finish(&ctx, md_buf);
    }

    int to_copy = (total_needed - generated < md_size) ? (total_needed - generated) : md_size;
    memcpy(output + generated, md_buf, to_copy);
    generated += to_copy;
  }

  mbedtls_md_free(&ctx);

  memcpy(key_out, output, key_len);
  memcpy(iv_out, output + key_len, iv_len);
  free(output);

  return key_len;
}

//
// Generic hash helper using mbedTLS message digest API
//
static void hash_data(VMContext& context, mbedtls_md_type_t md_type, int digest_len) {
  size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
  const long input_size = ((long)APITools_GetArraySize(input_array));
  const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);
  size_t* output_holder = APITools_GetArray(context, 0);

  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(md_type);
  if(!md_info) {
    output_holder[0] = 0;
    return;
  }

  unsigned char output[MBEDTLS_MD_MAX_SIZE];
  memset(output, 0, MBEDTLS_MD_MAX_SIZE);

  if(mbedtls_md(md_info, input, input_size, output) != 0) {
    output_holder[0] = 0;
    return;
  }

  // copy output
  size_t* output_byte_array = APITools_MakeByteArray(context, digest_len);
  unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
  memcpy(output_byte_array_buffer, output, digest_len * sizeof(unsigned char));
  output_holder[0] = (size_t)output_byte_array;
}

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
  }

  //
  // SHA-1 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_hash_sha1(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA1, SHA1_DIGEST_LEN);
  }

  //
  // SHA-256 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_hash_sha256(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA256, SHA256_DIGEST_LEN);
  }

  //
  // SHA-512 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_hash_sha512(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA512, SHA512_DIGEST_LEN);
  }

  //
  // RIPEMD160 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_hash_ripemd160(VMContext& context) {
    hash_data(context, MBEDTLS_MD_RIPEMD160, RIPEMD160_DIGEST_LEN);
  }

  //
  // MD5 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_hash_md5(VMContext& context) {
    hash_data(context, MBEDTLS_MD_MD5, MD5_DIGEST_LEN);
  }

  //
  // AES-256 encryption
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_encrypt_aes256(VMContext& context) {
    // get parameters
    size_t* key_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long key_size = ((long)APITools_GetArraySize(key_array));
    const unsigned char* key = (unsigned char*)APITools_GetArray(key_array);

    size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    size_t* output_holder = APITools_GetArray(context, 0);

    // TODO: add salt
    static const unsigned char salt[8] = { 0xA7, 0x3C, 0x91, 0x4E, 0x2B, 0xF5, 0xD8, 0x6A };

    // derive IV via EVP_BytesToKey (matches original OpenSSL behavior)
    unsigned char iv[32];
    unsigned char key_derived[32];
    if(evp_bytes_to_key(MBEDTLS_MD_SHA512, salt, 8, key, key_size, 8, key_derived, 32, iv, 16) != 32) {
      output_holder[0] = 0;
      return;
    }

    // use raw password bytes as key (matches original OpenSSL EVP_EncryptInit_ex behavior)
    unsigned char key_padded[32];
    memset(key_padded, 0, 32);
    memcpy(key_padded, key, (key_size < 32) ? key_size : 32);

    // encrypt
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);

    const mbedtls_cipher_info_t* cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if(!cipher_info) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_setup(&ctx, cipher_info) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_setkey(&ctx, key_padded, 256, MBEDTLS_ENCRYPT) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_set_iv(&ctx, iv, 16) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_reset(&ctx) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    int alloc_size = input_size + AES_BLOCK_LEN;
    unsigned char* output = (unsigned char*)calloc(alloc_size, sizeof(unsigned char));
    if(!output) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    size_t olen = 0;
    if(mbedtls_cipher_update(&ctx, input, input_size, output, &olen) != 0) {
      mbedtls_cipher_free(&ctx);
      free(output);
      output_holder[0] = 0;
      return;
    }

    size_t finish_olen = 0;
    if(mbedtls_cipher_finish(&ctx, output + olen, &finish_olen) != 0) {
      mbedtls_cipher_free(&ctx);
      free(output);
      output_holder[0] = 0;
      return;
    }

    mbedtls_cipher_free(&ctx);

    // copy output
    const int total_size = (int)(olen + finish_olen);
    size_t* output_byte_array = APITools_MakeByteArray(context, total_size);
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, output, total_size * sizeof(unsigned char));
    output_holder[0] = (size_t)output_byte_array;

    free(output);
  }

  //
  // AES-256 decryption
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_decrypt_aes256(VMContext& context) {
    // get parameters
    size_t* key_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long key_size = ((long)APITools_GetArraySize(key_array));
    const unsigned char* key = (unsigned char*)APITools_GetArray(key_array);

    size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
    const long input_size = (long)APITools_GetArraySize(input_array);
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    size_t* output_holder = APITools_GetArray(context, 0);

    // TODO: add salt
    static const unsigned char salt[8] = { 0xA7, 0x3C, 0x91, 0x4E, 0x2B, 0xF5, 0xD8, 0x6A };

    // derive IV via EVP_BytesToKey (matches original OpenSSL behavior)
    unsigned char iv[32];
    unsigned char key_derived[32];
    if(evp_bytes_to_key(MBEDTLS_MD_SHA512, salt, 8, key, key_size, 8, key_derived, 32, iv, 16) != 32) {
      output_holder[0] = 0;
      return;
    }

    // use raw password bytes as key (matches original OpenSSL EVP_DecryptInit_ex behavior)
    unsigned char key_padded[32];
    memset(key_padded, 0, 32);
    memcpy(key_padded, key, (key_size < 32) ? key_size : 32);

    // decrypt
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);

    const mbedtls_cipher_info_t* cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if(!cipher_info) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_setup(&ctx, cipher_info) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_setkey(&ctx, key_padded, 256, MBEDTLS_DECRYPT) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_set_iv(&ctx, iv, 16) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    if(mbedtls_cipher_reset(&ctx) != 0) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    int alloc_size = input_size + AES_BLOCK_LEN;
    unsigned char* output = (unsigned char*)calloc(alloc_size, sizeof(unsigned char));
    if(!output) {
      mbedtls_cipher_free(&ctx);
      output_holder[0] = 0;
      return;
    }

    size_t olen = 0;
    if(mbedtls_cipher_update(&ctx, input, input_size, output, &olen) != 0) {
      mbedtls_cipher_free(&ctx);
      free(output);
      output_holder[0] = 0;
      return;
    }

    size_t finish_olen = 0;
    if(mbedtls_cipher_finish(&ctx, output + olen, &finish_olen) != 0) {
      mbedtls_cipher_free(&ctx);
      free(output);
      output_holder[0] = 0;
      return;
    }

    mbedtls_cipher_free(&ctx);

    // copy output
    const int total_size = (int)(olen + finish_olen);
    size_t* output_byte_array = APITools_MakeByteArray(context, total_size);
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, output, total_size * sizeof(unsigned char));
    output_holder[0] = (size_t)output_byte_array;

    free(output);
  }

  //
  // Base64 encode
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_encrypt_base64(VMContext& context) {
    // get parameters
    size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    // determine output size
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, input, input_size);

    unsigned char* output = new unsigned char[olen + 1];
    output[olen] = '\0';

    if(mbedtls_base64_encode(output, olen, &olen, input, input_size) != 0) {
      delete[] output;
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    const std::wstring w_value(output, output + olen);
    APITools_SetStringValue(context, 0, w_value);

    delete[] output;
  }

  //
  // Base64 decode
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void openssl_decrypt_base64(VMContext& context) {
    const std::wstring w_input = APITools_GetStringValue(context, 1);
    const std::string input = UnicodeToBytes(w_input);

    // determine output size
    size_t olen = 0;
    mbedtls_base64_decode(nullptr, 0, &olen, (const unsigned char*)input.c_str(), input.size());

    unsigned char* buffer = new unsigned char[olen + 1];
    buffer[olen] = '\0';

    if(mbedtls_base64_decode(buffer, olen, &olen, (const unsigned char*)input.c_str(), input.size()) != 0) {
      delete[] buffer;
      size_t* output_holder = APITools_GetArray(context, 0);
      output_holder[0] = 0;
      return;
    }

    const size_t total_size = olen;
    size_t* output_holder = APITools_GetArray(context, 0);
    size_t* output_byte_array = APITools_MakeByteArray(context, total_size);
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, buffer, total_size * sizeof(unsigned char));
    output_holder[0] = (size_t)output_byte_array;

    delete[] buffer;
  }
}

#ifdef _MSYS2_CLANG
int main() { return 0; }
#endif
