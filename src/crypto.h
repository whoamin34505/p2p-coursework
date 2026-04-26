#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>

#define SHA256_HEX_LENGTH 65

int encrypt_file(const char *input_path, const char *output_path);
int decrypt_file(const char *input_path, const char *output_path);
int calculate_file_sha256(const char *file_path, char *hash_output, size_t hash_output_size);

#endif