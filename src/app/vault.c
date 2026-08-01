#include "vault.h"

#include <bcrypt.h>
#include <werapi.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    DIO_VAULT_HEADER_SIZE = 64,
    DIO_VAULT_MAX_BYTES = 1024 * 1024,
    DIO_VAULT_SALT_OFFSET = 16,
    DIO_VAULT_NONCE_OFFSET = 32,
    DIO_VAULT_TAG_OFFSET = 44,
    DIO_VAULT_LENGTH_OFFSET = 60
};

static const unsigned char DIO_VAULT_MAGIC[8] = {
    'D', 'I', 'O', 'V', 'L', 'T', '1', 0};

static void dio_vault_error(
    wchar_t *target,
    size_t capacity,
    const wchar_t *message) {
    if (target != NULL && capacity != 0u) {
        (void)wcsncpy_s(target, capacity, message, _TRUNCATE);
    }
}

static void dio_vault_write_u32(
    unsigned char *target,
    uint32_t value) {
    target[0] = (unsigned char)(value & 0xffu);
    target[1] = (unsigned char)((value >> 8u) & 0xffu);
    target[2] = (unsigned char)((value >> 16u) & 0xffu);
    target[3] = (unsigned char)((value >> 24u) & 0xffu);
}

static uint32_t dio_vault_read_u32(
    const unsigned char *source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8u) |
        ((uint32_t)source[2] << 16u) |
        ((uint32_t)source[3] << 24u);
}

static void dio_vault_clear_entries(DioVault *vault) {
    size_t index;
    if (vault == NULL) {
        return;
    }
    for (index = 0u; index < vault->entry_count; ++index) {
        if (vault->entries[index].value != NULL) {
            const size_t bytes =
                (wcslen(vault->entries[index].value) + 1u) *
                sizeof(wchar_t);
            if (vault->entries[index].excluded_from_wer) {
                (void)WerUnregisterExcludedMemoryBlock(
                    vault->entries[index].value);
            }
            SecureZeroMemory(vault->entries[index].value, bytes);
            free(vault->entries[index].value);
        }
        SecureZeroMemory(
            &vault->entries[index],
            sizeof(vault->entries[index]));
    }
    vault->entry_count = 0u;
}

void dio_vault_init(
    DioVault *vault,
    const wchar_t *path) {
    if (vault == NULL) {
        return;
    }
    ZeroMemory(vault, sizeof(*vault));
    if (path != NULL) {
        (void)wcsncpy_s(
            vault->path,
            _countof(vault->path),
            path,
            _TRUNCATE);
    }
}

void dio_vault_lock(DioVault *vault) {
    wchar_t path[MAX_PATH];
    if (vault == NULL) {
        return;
    }
    (void)wcscpy_s(path, _countof(path), vault->path);
    dio_vault_clear_entries(vault);
    if (vault->key_excluded_from_wer) {
        (void)WerUnregisterExcludedMemoryBlock(vault->key);
    }
    SecureZeroMemory(vault, sizeof(*vault));
    (void)wcscpy_s(vault->path, _countof(vault->path), path);
    SecureZeroMemory(path, sizeof(path));
}

bool dio_vault_exists(const DioVault *vault) {
    const DWORD attributes = vault != NULL && vault->path[0] != L'\0'
        ? GetFileAttributesW(vault->path)
        : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

static bool dio_vault_password_utf8(
    const wchar_t *password,
    unsigned char **bytes,
    ULONG *size) {
    int required;
    char *converted;
    if (password == NULL || wcslen(password) < 12u ||
        bytes == NULL || size == NULL) {
        return false;
    }
    required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        password,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (required <= 1 || (unsigned int)required > ULONG_MAX) {
        return false;
    }
    converted = (char *)malloc((size_t)required);
    if (converted == NULL ||
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            password,
            -1,
            converted,
            required,
            NULL,
            NULL) != required) {
        free(converted);
        return false;
    }
    *bytes = (unsigned char *)converted;
    *size = (ULONG)((unsigned int)required - 1u);
    return true;
}

static bool dio_vault_derive_key(
    const wchar_t *password,
    const unsigned char salt[16],
    unsigned char key[32]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    unsigned char *password_bytes = NULL;
    ULONG password_size = 0u;
    NTSTATUS status;
    bool result = false;
    if (!dio_vault_password_utf8(
            password,
            &password_bytes,
            &password_size)) {
        return false;
    }
    status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        NULL,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        status = BCryptDeriveKeyPBKDF2(
            algorithm,
            password_bytes,
            password_size,
            (PUCHAR)salt,
            16u,
            DIO_VAULT_PBKDF2_ITERATIONS,
            key,
            32u,
            0u);
        result = status >= 0;
    }
    if (algorithm != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0u);
    }
    SecureZeroMemory(password_bytes, password_size);
    free(password_bytes);
    return result;
}

static bool dio_vault_crypt(
    bool encrypt,
    const unsigned char key_bytes[32],
    const unsigned char nonce[12],
    unsigned char tag[16],
    const unsigned char *input,
    ULONG input_size,
    unsigned char *output) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    unsigned char *key_object = NULL;
    ULONG key_object_size = 0u;
    ULONG property_size = 0u;
    ULONG output_size = 0u;
    NTSTATUS status;
    bool result = false;

    status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_AES_ALGORITHM,
        NULL,
        0u);
    if (status < 0) {
        goto cleanup;
    }
    status = BCryptSetProperty(
        algorithm,
        BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
        (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM),
        0u);
    if (status < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&key_object_size,
            sizeof(key_object_size),
            &property_size,
            0u) < 0) {
        goto cleanup;
    }
    key_object = (unsigned char *)malloc(key_object_size);
    if (key_object == NULL ||
        BCryptGenerateSymmetricKey(
            algorithm,
            &key,
            key_object,
            key_object_size,
            (PUCHAR)key_bytes,
            32u,
            0u) < 0) {
        goto cleanup;
    }
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = (PUCHAR)nonce;
    auth.cbNonce = 12u;
    auth.pbTag = tag;
    auth.cbTag = 16u;
    status = encrypt
        ? BCryptEncrypt(
            key,
            (PUCHAR)input,
            input_size,
            &auth,
            NULL,
            0u,
            output,
            input_size,
            &output_size,
            0u)
        : BCryptDecrypt(
            key,
            (PUCHAR)input,
            input_size,
            &auth,
            NULL,
            0u,
            output,
            input_size,
            &output_size,
            0u);
    result = status >= 0 && output_size == input_size;

cleanup:
    if (key != NULL) {
        (void)BCryptDestroyKey(key);
    }
    if (key_object != NULL) {
        SecureZeroMemory(key_object, key_object_size);
        free(key_object);
    }
    if (algorithm != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0u);
    }
    return result;
}

static bool dio_vault_atomic_write(
    const wchar_t *path,
    const unsigned char *bytes,
    size_t size) {
    wchar_t temporary[MAX_PATH];
    HANDLE file;
    DWORD written = 0u;
    bool result = false;
    if (size > MAXDWORD ||
        swprintf_s(
            temporary,
            _countof(temporary),
            L"%ls.tmp",
            path) < 0) {
        return false;
    }
    file = CreateFileW(
        temporary,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    result = WriteFile(
            file,
            bytes,
            (DWORD)size,
            &written,
            NULL) &&
        written == (DWORD)size &&
        FlushFileBuffers(file);
    (void)CloseHandle(file);
    if (!result ||
        !MoveFileExW(
            temporary,
            path,
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary);
        return false;
    }
    return true;
}

static bool dio_vault_text_to_utf8(
    const wchar_t *text,
    char **utf8,
    uint32_t *size) {
    int required;
    char *bytes;
    if (text == NULL || utf8 == NULL || size == NULL) {
        return false;
    }
    required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (required <= 0 ||
        (uint64_t)(unsigned int)required - 1u > UINT32_MAX) {
        return false;
    }
    bytes = (char *)malloc((size_t)required);
    if (bytes == NULL ||
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            bytes,
            required,
            NULL,
            NULL) != required) {
        free(bytes);
        return false;
    }
    *utf8 = bytes;
    *size = (uint32_t)((unsigned int)required - 1u);
    return true;
}

static bool dio_vault_serialize(
    const DioVault *vault,
    unsigned char **plain,
    uint32_t *plain_size) {
    char *names[DIO_VAULT_MAX_ENTRIES] = {0};
    char *values[DIO_VAULT_MAX_ENTRIES] = {0};
    uint32_t name_sizes[DIO_VAULT_MAX_ENTRIES] = {0};
    uint32_t value_sizes[DIO_VAULT_MAX_ENTRIES] = {0};
    size_t total = 4u;
    unsigned char *bytes = NULL;
    size_t offset = 0u;
    size_t index;
    bool result = false;
    for (index = 0u; index < vault->entry_count; ++index) {
        if (!dio_vault_text_to_utf8(
                vault->entries[index].name,
                &names[index],
                &name_sizes[index]) ||
            !dio_vault_text_to_utf8(
                vault->entries[index].value,
                &values[index],
                &value_sizes[index]) ||
            total > UINT32_MAX - 8u -
                name_sizes[index] - value_sizes[index]) {
            goto cleanup;
        }
        total += 8u + name_sizes[index] + value_sizes[index];
    }
    bytes = (unsigned char *)malloc(total);
    if (bytes == NULL) {
        goto cleanup;
    }
    dio_vault_write_u32(bytes, (uint32_t)vault->entry_count);
    offset = 4u;
    for (index = 0u; index < vault->entry_count; ++index) {
        dio_vault_write_u32(bytes + offset, name_sizes[index]);
        dio_vault_write_u32(bytes + offset + 4u, value_sizes[index]);
        offset += 8u;
        memcpy(bytes + offset, names[index], name_sizes[index]);
        offset += name_sizes[index];
        memcpy(bytes + offset, values[index], value_sizes[index]);
        offset += value_sizes[index];
    }
    *plain = bytes;
    *plain_size = (uint32_t)total;
    bytes = NULL;
    result = true;

cleanup:
    free(bytes);
    for (index = 0u; index < vault->entry_count; ++index) {
        if (names[index] != NULL) {
            SecureZeroMemory(names[index], name_sizes[index]);
            free(names[index]);
        }
        if (values[index] != NULL) {
            SecureZeroMemory(values[index], value_sizes[index]);
            free(values[index]);
        }
    }
    return result;
}

static wchar_t *dio_vault_utf8_to_text(
    const unsigned char *bytes,
    uint32_t size,
    size_t maximum_chars) {
    int required;
    wchar_t *text;
    if (bytes == NULL || size > INT_MAX) {
        return NULL;
    }
    required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        (const char *)bytes,
        (int)size,
        NULL,
        0);
    if (required < 0 || (size_t)required >= maximum_chars) {
        return NULL;
    }
    text = (wchar_t *)calloc(
        (size_t)required + 1u,
        sizeof(*text));
    if (text == NULL ||
        (required != 0 &&
         MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            (const char *)bytes,
            (int)size,
            text,
            required) != required)) {
        free(text);
        return NULL;
    }
    return text;
}

static bool dio_vault_parse(
    DioVault *vault,
    const unsigned char *plain,
    uint32_t plain_size) {
    uint32_t count;
    size_t offset = 4u;
    uint32_t index;
    if (plain == NULL || plain_size < 4u) {
        return false;
    }
    count = dio_vault_read_u32(plain);
    if (count > DIO_VAULT_MAX_ENTRIES) {
        return false;
    }
    dio_vault_clear_entries(vault);
    for (index = 0u; index < count; ++index) {
        uint32_t name_size;
        uint32_t value_size;
        wchar_t *name;
        wchar_t *value;
        if (offset > plain_size || plain_size - offset < 8u) {
            goto invalid;
        }
        name_size = dio_vault_read_u32(plain + offset);
        value_size = dio_vault_read_u32(plain + offset + 4u);
        offset += 8u;
        if (name_size == 0u ||
            value_size > DIO_VAULT_VALUE_CAP * 4u ||
            offset > plain_size ||
            name_size > plain_size - offset ||
            value_size > plain_size - offset - name_size) {
            goto invalid;
        }
        name = dio_vault_utf8_to_text(
            plain + offset,
            name_size,
            DIO_VAULT_NAME_CAP);
        offset += name_size;
        value = dio_vault_utf8_to_text(
            plain + offset,
            value_size,
            DIO_VAULT_VALUE_CAP);
        offset += value_size;
        if (name == NULL || value == NULL) {
            free(name);
            free(value);
            goto invalid;
        }
        (void)wcscpy_s(
            vault->entries[index].name,
            _countof(vault->entries[index].name),
            name);
        SecureZeroMemory(
            name,
            (wcslen(name) + 1u) * sizeof(*name));
        free(name);
        vault->entries[index].value = value;
        vault->entries[index].excluded_from_wer = SUCCEEDED(
            WerRegisterExcludedMemoryBlock(
                value,
                (DWORD)((wcslen(value) + 1u) * sizeof(*value))));
        vault->entry_count += 1u;
    }
    return offset == plain_size;

invalid:
    dio_vault_clear_entries(vault);
    return false;
}

bool dio_vault_save(
    DioVault *vault,
    wchar_t *error,
    size_t error_capacity) {
    unsigned char nonce[12];
    unsigned char tag[16] = {0};
    unsigned char *plain = NULL;
    unsigned char *file = NULL;
    uint32_t plain_size = 0u;
    bool result = false;
    if (vault == NULL || !vault->unlocked ||
        vault->path[0] == L'\0') {
        dio_vault_error(error, error_capacity, L"Secret vault is locked.");
        return false;
    }
    if (!dio_vault_serialize(vault, &plain, &plain_size) ||
        plain_size > DIO_VAULT_MAX_BYTES - DIO_VAULT_HEADER_SIZE ||
        BCryptGenRandom(
            NULL,
            nonce,
            sizeof(nonce),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        dio_vault_error(error, error_capacity, L"Could not prepare the secret vault.");
        goto cleanup;
    }
    file = (unsigned char *)calloc(
        DIO_VAULT_HEADER_SIZE + (size_t)plain_size,
        1u);
    if (file == NULL) {
        dio_vault_error(error, error_capacity, L"Out of memory.");
        goto cleanup;
    }
    memcpy(file, DIO_VAULT_MAGIC, sizeof(DIO_VAULT_MAGIC));
    dio_vault_write_u32(file + 8u, 1u);
    dio_vault_write_u32(
        file + 12u,
        DIO_VAULT_PBKDF2_ITERATIONS);
    memcpy(file + DIO_VAULT_SALT_OFFSET, vault->salt, sizeof(vault->salt));
    memcpy(file + DIO_VAULT_NONCE_OFFSET, nonce, sizeof(nonce));
    dio_vault_write_u32(file + DIO_VAULT_LENGTH_OFFSET, plain_size);
    if (!dio_vault_crypt(
            true,
            vault->key,
            nonce,
            tag,
            plain,
            plain_size,
            file + DIO_VAULT_HEADER_SIZE)) {
        dio_vault_error(error, error_capacity, L"Could not encrypt the secret vault.");
        goto cleanup;
    }
    memcpy(file + DIO_VAULT_TAG_OFFSET, tag, sizeof(tag));
    result = dio_vault_atomic_write(
        vault->path,
        file,
        DIO_VAULT_HEADER_SIZE + (size_t)plain_size);
    if (!result) {
        dio_vault_error(error, error_capacity, L"Could not save secrets.bin.");
    }

cleanup:
    SecureZeroMemory(nonce, sizeof(nonce));
    SecureZeroMemory(tag, sizeof(tag));
    if (plain != NULL) {
        SecureZeroMemory(plain, plain_size);
        free(plain);
    }
    if (file != NULL) {
        SecureZeroMemory(
            file,
            DIO_VAULT_HEADER_SIZE + (size_t)plain_size);
        free(file);
    }
    return result;
}

bool dio_vault_create(
    DioVault *vault,
    const wchar_t *master_password,
    wchar_t *error,
    size_t error_capacity) {
    if (vault == NULL || vault->path[0] == L'\0' ||
        dio_vault_exists(vault)) {
        dio_vault_error(error, error_capacity, L"Secret vault already exists.");
        return false;
    }
    dio_vault_clear_entries(vault);
    if (BCryptGenRandom(
            NULL,
            vault->salt,
            sizeof(vault->salt),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !dio_vault_derive_key(
            master_password,
            vault->salt,
            vault->key)) {
        SecureZeroMemory(vault->salt, sizeof(vault->salt));
        SecureZeroMemory(vault->key, sizeof(vault->key));
        dio_vault_error(
            error,
            error_capacity,
            L"Master password must contain at least 12 characters.");
        return false;
    }
    vault->unlocked = true;
    vault->key_excluded_from_wer = SUCCEEDED(
        WerRegisterExcludedMemoryBlock(
            vault->key,
            (DWORD)sizeof(vault->key)));
    if (!dio_vault_save(vault, error, error_capacity)) {
        dio_vault_lock(vault);
        return false;
    }
    return true;
}

static bool dio_vault_read_file(
    const wchar_t *path,
    unsigned char **bytes,
    size_t *size) {
    HANDLE file;
    LARGE_INTEGER length;
    DWORD read = 0u;
    unsigned char *buffer;
    bool result;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileSizeEx(file, &length) ||
        length.QuadPart < DIO_VAULT_HEADER_SIZE ||
        length.QuadPart > DIO_VAULT_MAX_BYTES) {
        if (file != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(file);
        }
        return false;
    }
    buffer = (unsigned char *)malloc((size_t)length.QuadPart);
    result = buffer != NULL &&
        ReadFile(
            file,
            buffer,
            (DWORD)length.QuadPart,
            &read,
            NULL) &&
        read == (DWORD)length.QuadPart;
    (void)CloseHandle(file);
    if (!result) {
        free(buffer);
        return false;
    }
    *bytes = buffer;
    *size = (size_t)length.QuadPart;
    return true;
}

bool dio_vault_unlock(
    DioVault *vault,
    const wchar_t *master_password,
    wchar_t *error,
    size_t error_capacity) {
    unsigned char *file = NULL;
    unsigned char *plain = NULL;
    size_t file_size = 0u;
    uint32_t cipher_size = 0u;
    unsigned char tag[16] = {0};
    bool result = false;
    if (vault == NULL || vault->path[0] == L'\0' ||
        !dio_vault_read_file(vault->path, &file, &file_size)) {
        dio_vault_error(error, error_capacity, L"Could not read secrets.bin.");
        return false;
    }
    if (file_size < DIO_VAULT_HEADER_SIZE ||
        memcmp(file, DIO_VAULT_MAGIC, sizeof(DIO_VAULT_MAGIC)) != 0 ||
        dio_vault_read_u32(file + 8u) != 1u ||
        dio_vault_read_u32(file + 12u) !=
            DIO_VAULT_PBKDF2_ITERATIONS) {
        dio_vault_error(error, error_capacity, L"secrets.bin has an unsupported format.");
        goto cleanup;
    }
    cipher_size = dio_vault_read_u32(
        file + DIO_VAULT_LENGTH_OFFSET);
    if ((size_t)cipher_size != file_size - DIO_VAULT_HEADER_SIZE) {
        dio_vault_error(error, error_capacity, L"secrets.bin is truncated.");
        goto cleanup;
    }
    memcpy(vault->salt, file + DIO_VAULT_SALT_OFFSET, sizeof(vault->salt));
    if (!dio_vault_derive_key(
            master_password,
            vault->salt,
            vault->key)) {
        dio_vault_error(error, error_capacity, L"Master password is invalid.");
        goto cleanup;
    }
    vault->key_excluded_from_wer = SUCCEEDED(
        WerRegisterExcludedMemoryBlock(
            vault->key,
            (DWORD)sizeof(vault->key)));
    plain = (unsigned char *)malloc(cipher_size);
    memcpy(tag, file + DIO_VAULT_TAG_OFFSET, sizeof(tag));
    if (plain == NULL ||
        !dio_vault_crypt(
            false,
            vault->key,
            file + DIO_VAULT_NONCE_OFFSET,
            tag,
            file + DIO_VAULT_HEADER_SIZE,
            cipher_size,
            plain) ||
        !dio_vault_parse(vault, plain, cipher_size)) {
        if (vault->key_excluded_from_wer) {
            (void)WerUnregisterExcludedMemoryBlock(vault->key);
            vault->key_excluded_from_wer = false;
        }
        SecureZeroMemory(vault->key, sizeof(vault->key));
        SecureZeroMemory(vault->salt, sizeof(vault->salt));
        dio_vault_error(error, error_capacity, L"Master password is invalid or the vault is damaged.");
        goto cleanup;
    }
    vault->unlocked = true;
    result = true;

cleanup:
    SecureZeroMemory(tag, sizeof(tag));
    if (plain != NULL) {
        SecureZeroMemory(plain, cipher_size);
        free(plain);
    }
    if (file != NULL) {
        SecureZeroMemory(file, file_size);
        free(file);
    }
    return result;
}

bool dio_vault_change_password(
    DioVault *vault,
    const wchar_t *new_master_password,
    wchar_t *error,
    size_t error_capacity) {
    unsigned char new_salt[16];
    unsigned char new_key[32];
    unsigned char old_salt[16];
    unsigned char old_key[32];
    bool result;
    if (vault == NULL || !vault->unlocked ||
        BCryptGenRandom(
            NULL,
            new_salt,
            sizeof(new_salt),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !dio_vault_derive_key(
            new_master_password,
            new_salt,
            new_key)) {
        dio_vault_error(
            error,
            error_capacity,
            L"New master password must contain at least 12 characters.");
        return false;
    }
    memcpy(old_salt, vault->salt, sizeof(old_salt));
    memcpy(old_key, vault->key, sizeof(old_key));
    memcpy(vault->salt, new_salt, sizeof(new_salt));
    memcpy(vault->key, new_key, sizeof(new_key));
    result = dio_vault_save(vault, error, error_capacity);
    if (!result) {
        memcpy(vault->salt, old_salt, sizeof(old_salt));
        memcpy(vault->key, old_key, sizeof(old_key));
    }
    if (!vault->key_excluded_from_wer) {
        vault->key_excluded_from_wer = SUCCEEDED(
            WerRegisterExcludedMemoryBlock(
                vault->key,
                (DWORD)sizeof(vault->key)));
    }
    SecureZeroMemory(new_salt, sizeof(new_salt));
    SecureZeroMemory(new_key, sizeof(new_key));
    SecureZeroMemory(old_salt, sizeof(old_salt));
    SecureZeroMemory(old_key, sizeof(old_key));
    return result;
}

bool dio_vault_reset(
    DioVault *vault,
    wchar_t *error,
    size_t error_capacity) {
    wchar_t path[MAX_PATH];
    if (vault == NULL || vault->path[0] == L'\0') {
        dio_vault_error(error, error_capacity, L"Invalid secret vault path.");
        return false;
    }
    (void)wcscpy_s(path, _countof(path), vault->path);
    if (!DeleteFileW(path) &&
        GetLastError() != ERROR_FILE_NOT_FOUND) {
        dio_vault_error(error, error_capacity, L"Could not reset secrets.bin.");
        return false;
    }
    dio_vault_lock(vault);
    (void)wcscpy_s(vault->path, _countof(vault->path), path);
    SecureZeroMemory(path, sizeof(path));
    return true;
}

static size_t dio_vault_find(
    const DioVault *vault,
    const wchar_t *name) {
    size_t index;
    if (vault == NULL || name == NULL) {
        return SIZE_MAX;
    }
    for (index = 0u; index < vault->entry_count; ++index) {
        if (wcscmp(vault->entries[index].name, name) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

const wchar_t *dio_vault_get(
    const DioVault *vault,
    const wchar_t *name) {
    const size_t index = vault != NULL && vault->unlocked
        ? dio_vault_find(vault, name)
        : SIZE_MAX;
    return index != SIZE_MAX ? vault->entries[index].value : NULL;
}

bool dio_vault_set(
    DioVault *vault,
    const wchar_t *name,
    const wchar_t *value,
    wchar_t *error,
    size_t error_capacity) {
    size_t index;
    wchar_t *copy;
    const size_t value_length = value != NULL ? wcslen(value) : 0u;
    if (vault == NULL || !vault->unlocked ||
        name == NULL || name[0] == L'\0' ||
        wcslen(name) >= DIO_VAULT_NAME_CAP ||
        value == NULL || value_length >= DIO_VAULT_VALUE_CAP) {
        dio_vault_error(error, error_capacity, L"Invalid secret name or value.");
        return false;
    }
    index = dio_vault_find(vault, name);
    if (index == SIZE_MAX) {
        if (vault->entry_count >= DIO_VAULT_MAX_ENTRIES) {
            dio_vault_error(error, error_capacity, L"Secret vault is full.");
            return false;
        }
        index = vault->entry_count++;
        (void)wcscpy_s(
            vault->entries[index].name,
            _countof(vault->entries[index].name),
            name);
    }
    copy = (wchar_t *)malloc(
        (value_length + 1u) * sizeof(*copy));
    if (copy == NULL) {
        dio_vault_error(error, error_capacity, L"Out of memory.");
        return false;
    }
    memcpy(copy, value, (value_length + 1u) * sizeof(*copy));
    if (vault->entries[index].value != NULL) {
        if (vault->entries[index].excluded_from_wer) {
            (void)WerUnregisterExcludedMemoryBlock(
                vault->entries[index].value);
        }
        SecureZeroMemory(
            vault->entries[index].value,
            (wcslen(vault->entries[index].value) + 1u) *
                sizeof(wchar_t));
        free(vault->entries[index].value);
    }
    vault->entries[index].value = copy;
    vault->entries[index].excluded_from_wer = SUCCEEDED(
        WerRegisterExcludedMemoryBlock(
            copy,
            (DWORD)((value_length + 1u) * sizeof(*copy))));
    return true;
}

bool dio_vault_remove(
    DioVault *vault,
    const wchar_t *name,
    wchar_t *error,
    size_t error_capacity) {
    size_t index;
    if (vault == NULL || !vault->unlocked) {
        dio_vault_error(error, error_capacity, L"Secret vault is locked.");
        return false;
    }
    index = dio_vault_find(vault, name);
    if (index == SIZE_MAX) {
        return true;
    }
    if (vault->entries[index].value != NULL) {
        if (vault->entries[index].excluded_from_wer) {
            (void)WerUnregisterExcludedMemoryBlock(
                vault->entries[index].value);
        }
        SecureZeroMemory(
            vault->entries[index].value,
            (wcslen(vault->entries[index].value) + 1u) *
                sizeof(wchar_t));
        free(vault->entries[index].value);
    }
    for (; index + 1u < vault->entry_count; ++index) {
        vault->entries[index] = vault->entries[index + 1u];
    }
    vault->entry_count -= 1u;
    ZeroMemory(
        &vault->entries[vault->entry_count],
        sizeof(vault->entries[vault->entry_count]));
    return true;
}
