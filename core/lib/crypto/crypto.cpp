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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>

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

//
// Digests a file a megabyte at a time, so a file of any size is hashed without being
// held in memory (a downloaded model is hundreds of megabytes). Nil when the file cannot
// be opened or read.
//
static void hash_file(VMContext& context, mbedtls_md_type_t md_type, int digest_len) {
  size_t* output_holder = APITools_GetArray(context, 0);
  output_holder[0] = 0;

  const std::wstring w_path = APITools_GetStringValue(context, 1);
#ifdef _WIN32
  FILE* file = nullptr;
  if(_wfopen_s(&file, w_path.c_str(), L"rb") != 0) {
    file = nullptr;
  }
#else
  FILE* file = fopen(UnicodeToBytes(w_path).c_str(), "rb");
#endif
  if(!file) {
    return;
  }

  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(md_type);
  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  bool ok = md_info && mbedtls_md_setup(&md, md_info, 0) == 0 && mbedtls_md_starts(&md) == 0;

  const size_t block_size = 1024 * 1024;
  unsigned char* block = new unsigned char[block_size];
  while(ok) {
    const size_t read = fread(block, 1, block_size, file);
    if(read > 0 && mbedtls_md_update(&md, block, read) != 0) {
      ok = false;
    }
    if(read < block_size) {
      if(ferror(file)) {
        ok = false;
      }
      break;
    }
  }
  delete[] block;
  fclose(file);

  unsigned char output[MBEDTLS_MD_MAX_SIZE];
  if(ok && mbedtls_md_finish(&md, output) == 0) {
    size_t* output_byte_array = APITools_MakeByteArray(context, digest_len);
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, output, digest_len * sizeof(unsigned char));
    output_holder[0] = (size_t)output_byte_array;
  }
  mbedtls_md_free(&md);
}

//
// Versioned AES-256 format (see crypto_encrypt_aes256)
//
#define CIPHER_VERSION                1
#define CIPHER_SALT_LEN               16
#define CIPHER_NONCE_LEN              12
#define CIPHER_TAG_LEN                16
#define CIPHER_HEADER_LEN             (4 + 1 + 4 + CIPHER_SALT_LEN + CIPHER_NONCE_LEN)
#define CIPHER_PBKDF2_ITERATIONS      210000u
#define CIPHER_PBKDF2_MAX_ITERATIONS  4000000u
static const unsigned char CIPHER_MAGIC[4] = { 'O', 'B', 'K', 'C' };

// fill buffer from the CSPRNG (mbedTLS CTR_DRBG seeded from the OS entropy source)
static bool random_bytes(unsigned char* buf, size_t len) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);

  static const unsigned char pers[] = "objeck-cipher";
  bool ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, pers, sizeof(pers) - 1) == 0 &&
            mbedtls_ctr_drbg_random(&drbg, buf, len) == 0;

  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  return ok;
}

// PBKDF2-HMAC-SHA256 (RFC 8018), built on mbedtls_md_hmac_* so it compiles
// against both mbedTLS 2.x and 3.x (the library's own PBKDF2 entry points differ)
static bool pbkdf2_sha256(const unsigned char* password, size_t password_len,
                          const unsigned char* salt, size_t salt_len,
                          unsigned int iterations,
                          unsigned char* out, size_t out_len)
{
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!md_info || iterations < 1) {
    return false;
  }

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  if(mbedtls_md_setup(&ctx, md_info, 1) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  const size_t hlen = SHA256_DIGEST_LEN;
  unsigned char u[SHA256_DIGEST_LEN];
  unsigned char t[SHA256_DIGEST_LEN];
  bool ok = mbedtls_md_hmac_starts(&ctx, password, password_len) == 0;

  for(unsigned int block = 1; ok && (size_t)(block - 1) * hlen < out_len; ++block) {
    unsigned char counter[4] = { (unsigned char)(block >> 24), (unsigned char)(block >> 16),
                                 (unsigned char)(block >> 8), (unsigned char)block };

    ok = mbedtls_md_hmac_reset(&ctx) == 0 &&
         mbedtls_md_hmac_update(&ctx, salt, salt_len) == 0 &&
         mbedtls_md_hmac_update(&ctx, counter, 4) == 0 &&
         mbedtls_md_hmac_finish(&ctx, u) == 0;
    if(!ok) {
      break;
    }
    memcpy(t, u, hlen);

    for(unsigned int i = 1; i < iterations; ++i) {
      ok = mbedtls_md_hmac_reset(&ctx) == 0 &&
           mbedtls_md_hmac_update(&ctx, u, hlen) == 0 &&
           mbedtls_md_hmac_finish(&ctx, u) == 0;
      if(!ok) {
        break;
      }
      for(size_t j = 0; j < hlen; ++j) {
        t[j] ^= u[j];
      }
    }

    const size_t offset = (size_t)(block - 1) * hlen;
    const size_t n = (out_len - offset < hlen) ? out_len - offset : hlen;
    memcpy(out + offset, t, n);
  }

  mbedtls_platform_zeroize(u, sizeof(u));
  mbedtls_platform_zeroize(t, sizeof(t));
  mbedtls_md_free(&ctx);
  return ok;
}

//
// Legacy AES-256-CBC decryption (decrypt-only, for data written by older
// releases): key is the raw password zero-padded/truncated to 32 bytes, IV is
// derived from the password with a fixed salt, and there is no authentication.
//
static void legacy_decrypt_aes256(VMContext& context, const unsigned char* key, long key_size,
                                  const unsigned char* input, long input_size, size_t* output_holder)
{
  static const unsigned char salt[8] = { 0xA7, 0x3C, 0x91, 0x4E, 0x2B, 0xF5, 0xD8, 0x6A };

  unsigned char iv[32];
  unsigned char key_derived[32];
  if(evp_bytes_to_key(MBEDTLS_MD_SHA512, salt, 8, key, key_size, 8, key_derived, 32, iv, 16) != 32) {
    output_holder[0] = 0;
    return;
  }

  unsigned char key_padded[32];
  memset(key_padded, 0, 32);
  memcpy(key_padded, key, (key_size < 32) ? key_size : 32);

  mbedtls_cipher_context_t ctx;
  mbedtls_cipher_init(&ctx);

  const mbedtls_cipher_info_t* cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
  if(!cipher_info || mbedtls_cipher_setup(&ctx, cipher_info) != 0 ||
     mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_PKCS7) != 0 ||
     mbedtls_cipher_setkey(&ctx, key_padded, 256, MBEDTLS_DECRYPT) != 0 ||
     mbedtls_cipher_set_iv(&ctx, iv, 16) != 0 ||
     mbedtls_cipher_reset(&ctx) != 0) {
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
  size_t finish_olen = 0;
  if(mbedtls_cipher_update(&ctx, input, input_size, output, &olen) != 0 ||
     mbedtls_cipher_finish(&ctx, output + olen, &finish_olen) != 0) {
    mbedtls_cipher_free(&ctx);
    free(output);
    output_holder[0] = 0;
    return;
  }

  mbedtls_cipher_free(&ctx);

  const int total_size = (int)(olen + finish_olen);
  size_t* output_byte_array = APITools_MakeByteArray(context, total_size);
  unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
  memcpy(output_byte_array_buffer, output, total_size * sizeof(unsigned char));
  output_holder[0] = (size_t)output_byte_array;

  free(output);
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
  void crypto_hash_sha1(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA1, SHA1_DIGEST_LEN);
  }

  //
  // SHA-256 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_hash_sha256(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA256, SHA256_DIGEST_LEN);
  }

  //
  // SHA-256 of a file, streamed
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_hash_sha256_file(VMContext& context) {
    hash_file(context, MBEDTLS_MD_SHA256, SHA256_DIGEST_LEN);
  }

  //
  // SHA-512 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_hash_sha512(VMContext& context) {
    hash_data(context, MBEDTLS_MD_SHA512, SHA512_DIGEST_LEN);
  }

  //
  // RIPEMD160 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_hash_ripemd160(VMContext& context) {
    hash_data(context, MBEDTLS_MD_RIPEMD160, RIPEMD160_DIGEST_LEN);
  }

  //
  // MD5 hash
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_hash_md5(VMContext& context) {
    hash_data(context, MBEDTLS_MD_MD5, MD5_DIGEST_LEN);
  }

  //
  // AES-256 encryption (versioned format, AES-256-GCM)
  //
  // Output layout (all multi-byte integers big-endian):
  //   "OBKC"              4  magic
  //   version             1  format version (1)
  //   iterations          4  PBKDF2-HMAC-SHA256 iteration count
  //   salt               16  random per message
  //   nonce              12  random per message
  //   ciphertext          n  same length as the plaintext
  //   tag                16  GCM authentication tag
  // The header (everything before the ciphertext) is the GCM additional
  // authenticated data, so the iteration count and salt cannot be altered.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_encrypt_aes256(VMContext& context) {
    // get parameters
    size_t* key_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long key_size = ((long)APITools_GetArraySize(key_array));
    const unsigned char* key = (unsigned char*)APITools_GetArray(key_array);

    size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    size_t* output_holder = APITools_GetArray(context, 0);
    output_holder[0] = 0;

    // header: magic, version, iterations, salt, nonce
    unsigned char header[CIPHER_HEADER_LEN];
    memcpy(header, CIPHER_MAGIC, 4);
    header[4] = CIPHER_VERSION;
    header[5] = (unsigned char)((CIPHER_PBKDF2_ITERATIONS >> 24) & 0xFF);
    header[6] = (unsigned char)((CIPHER_PBKDF2_ITERATIONS >> 16) & 0xFF);
    header[7] = (unsigned char)((CIPHER_PBKDF2_ITERATIONS >> 8) & 0xFF);
    header[8] = (unsigned char)(CIPHER_PBKDF2_ITERATIONS & 0xFF);
    if(!random_bytes(header + 9, CIPHER_SALT_LEN + CIPHER_NONCE_LEN)) {
      return;
    }

    unsigned char aes_key[32];
    if(!pbkdf2_sha256(key, key_size, header + 9, CIPHER_SALT_LEN, CIPHER_PBKDF2_ITERATIONS, aes_key, 32)) {
      return;
    }

    const size_t total_size = CIPHER_HEADER_LEN + (size_t)input_size + CIPHER_TAG_LEN;
    unsigned char* output = (unsigned char*)calloc(total_size, sizeof(unsigned char));
    if(!output) {
      mbedtls_platform_zeroize(aes_key, sizeof(aes_key));
      return;
    }
    memcpy(output, header, CIPHER_HEADER_LEN);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256) == 0 &&
      mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, (size_t)input_size,
                                header + 9 + CIPHER_SALT_LEN, CIPHER_NONCE_LEN,
                                header, CIPHER_HEADER_LEN,
                                input, output + CIPHER_HEADER_LEN,
                                CIPHER_TAG_LEN, output + CIPHER_HEADER_LEN + input_size) == 0;
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(aes_key, sizeof(aes_key));

    if(ok) {
      size_t* output_byte_array = APITools_MakeByteArray(context, (int)total_size);
      unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
      memcpy(output_byte_array_buffer, output, total_size);
      output_holder[0] = (size_t)output_byte_array;
    }

    free(output);
  }

  //
  // AES-256 decryption. Input with the versioned header is authenticated
  // (AES-256-GCM); a bad tag, wrong password or truncated input yields Nil and
  // never plaintext. Input without the header is the legacy AES-256-CBC format.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_decrypt_aes256(VMContext& context) {
    // get parameters
    size_t* key_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long key_size = ((long)APITools_GetArraySize(key_array));
    const unsigned char* key = (unsigned char*)APITools_GetArray(key_array);

    size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
    const long input_size = (long)APITools_GetArraySize(input_array);
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    size_t* output_holder = APITools_GetArray(context, 0);
    output_holder[0] = 0;

    if(input_size >= 4 && memcmp(input, CIPHER_MAGIC, 4) == 0) {
      // versioned format; once the magic matches there is no fallback to legacy
      if(input_size < CIPHER_HEADER_LEN + CIPHER_TAG_LEN || input[4] != CIPHER_VERSION) {
        return;
      }

      const unsigned int iterations = ((unsigned int)input[5] << 24) | ((unsigned int)input[6] << 16) |
                                      ((unsigned int)input[7] << 8) | (unsigned int)input[8];
      if(iterations < 1 || iterations > CIPHER_PBKDF2_MAX_ITERATIONS) {
        return;
      }

      unsigned char aes_key[32];
      if(!pbkdf2_sha256(key, key_size, input + 9, CIPHER_SALT_LEN, iterations, aes_key, 32)) {
        return;
      }

      const size_t plain_size = (size_t)input_size - CIPHER_HEADER_LEN - CIPHER_TAG_LEN;
      unsigned char* output = (unsigned char*)calloc(plain_size + 1, sizeof(unsigned char));
      if(!output) {
        mbedtls_platform_zeroize(aes_key, sizeof(aes_key));
        return;
      }

      mbedtls_gcm_context gcm;
      mbedtls_gcm_init(&gcm);
      bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&gcm, plain_size,
                                 input + 9 + CIPHER_SALT_LEN, CIPHER_NONCE_LEN,
                                 input, CIPHER_HEADER_LEN,
                                 input + input_size - CIPHER_TAG_LEN, CIPHER_TAG_LEN,
                                 input + CIPHER_HEADER_LEN, output) == 0;
      mbedtls_gcm_free(&gcm);
      mbedtls_platform_zeroize(aes_key, sizeof(aes_key));

      if(ok) {
        size_t* output_byte_array = APITools_MakeByteArray(context, (int)plain_size);
        unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
        memcpy(output_byte_array_buffer, output, plain_size);
        output_holder[0] = (size_t)output_byte_array;
      }

      mbedtls_platform_zeroize(output, plain_size);
      free(output);
      return;
    }

    legacy_decrypt_aes256(context, key, key_size, input, input_size, output_holder);
  }

  //
  // Base64 encode
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void crypto_encrypt_base64(VMContext& context) {
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
  void crypto_decrypt_base64(VMContext& context) {
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
