#include "crypto.h"
#include "logger.h"

#include <stdio.h>
#include <openssl/evp.h>

#define CRYPTO_BUFFER_SIZE 4096

static const unsigned char KEY[32] = "0123456789abcdef0123456789abcdef";
static const unsigned char IV[16] = "abcdef9876543210";

static int process_file(const char *input_path, const char *output_path, int encrypt_mode) {
    FILE *input_file;
    FILE *output_file;
    EVP_CIPHER_CTX *ctx;

    unsigned char input_buffer[CRYPTO_BUFFER_SIZE];
    unsigned char output_buffer[CRYPTO_BUFFER_SIZE + EVP_MAX_BLOCK_LENGTH];

    int input_length;
    int output_length;
    int result;

    input_file = fopen(input_path, "rb");
    if (input_file == NULL) {
        log_message("ERROR", "Cannot open input file: %s", input_path);
        return -1;
    }

    output_file = fopen(output_path, "wb");
    if (output_file == NULL) {
        fclose(input_file);
        log_message("ERROR", "Cannot open output file: %s", output_path);
        return -1;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        fclose(input_file);
        fclose(output_file);
        log_message("ERROR", "Cannot create crypto context");
        return -1;
    }

    result = EVP_CipherInit_ex(ctx, EVP_aes_256_cbc(), NULL, KEY, IV, encrypt_mode);
    if (result != 1) {
        EVP_CIPHER_CTX_free(ctx);
        fclose(input_file);
        fclose(output_file);
        log_message("ERROR", "Cannot initialize crypto operation");
        return -1;
    }

    while ((input_length = fread(input_buffer, 1, CRYPTO_BUFFER_SIZE, input_file)) > 0) {
        result = EVP_CipherUpdate(ctx, output_buffer, &output_length, input_buffer, input_length);
        if (result != 1) {
            EVP_CIPHER_CTX_free(ctx);
            fclose(input_file);
            fclose(output_file);
            log_message("ERROR", "Crypto update error");
            return -1;
        }

        fwrite(output_buffer, 1, output_length, output_file);
    }

    result = EVP_CipherFinal_ex(ctx, output_buffer, &output_length);
    if (result != 1) {
        EVP_CIPHER_CTX_free(ctx);
        fclose(input_file);
        fclose(output_file);
        log_message("ERROR", "Crypto finalization error");
        return -1;
    }

    fwrite(output_buffer, 1, output_length, output_file);

    EVP_CIPHER_CTX_free(ctx);
    fclose(input_file);
    fclose(output_file);

    return 0;
}

int encrypt_file(const char *input_path, const char *output_path) {
    // Шифруем файл перед отправкой по сети
    return process_file(input_path, output_path, 1);
}

int decrypt_file(const char *input_path, const char *output_path) {
    // Расшифровываем файл после получения по сети
    return process_file(input_path, output_path, 0);
}

int calculate_file_sha256(const char *file_path, char *hash_output, size_t hash_output_size) {
    FILE *file;
    EVP_MD_CTX *ctx;
    unsigned char buffer[CRYPTO_BUFFER_SIZE];
    unsigned char hash[EVP_MAX_MD_SIZE];

    size_t bytes_read;
    unsigned int hash_length;
    unsigned int i;

    if (hash_output_size < SHA256_HEX_LENGTH) {
        log_message("ERROR", "SHA-256 output buffer is too small");
        return -1;
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        log_message("ERROR", "Cannot open file for SHA-256: %s", file_path);
        return -1;
    }

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        fclose(file);
        log_message("ERROR", "Cannot create SHA-256 context");
        return -1;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        fclose(file);
        log_message("ERROR", "Cannot initialize SHA-256");
        return -1;
    }

    while ((bytes_read = fread(buffer, 1, CRYPTO_BUFFER_SIZE, file)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
            EVP_MD_CTX_free(ctx);
            fclose(file);
            log_message("ERROR", "SHA-256 update error");
            return -1;
        }
    }

    if (EVP_DigestFinal_ex(ctx, hash, &hash_length) != 1) {
        EVP_MD_CTX_free(ctx);
        fclose(file);
        log_message("ERROR", "SHA-256 finalization error");
        return -1;
    }

    EVP_MD_CTX_free(ctx);
    fclose(file);

    for (i = 0; i < hash_length; i++) {
        snprintf(hash_output + (i * 2), 3, "%02x", hash[i]);
    }

    hash_output[SHA256_HEX_LENGTH - 1] = '\0';

    return 0;
}