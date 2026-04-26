#ifndef CRYPTO_H
#define CRYPTO_H

int encrypt_file(const char *input_path, const char *output_path);
int decrypt_file(const char *input_path, const char *output_path);

#endif