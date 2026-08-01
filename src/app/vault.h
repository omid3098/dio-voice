#ifndef DIO_VOICE_VAULT_H
#define DIO_VOICE_VAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

enum {
    DIO_VAULT_MAX_ENTRIES = 32,
    DIO_VAULT_NAME_CAP = 64,
    DIO_VAULT_VALUE_CAP = 4096,
    DIO_VAULT_PBKDF2_ITERATIONS = 600000
};

typedef struct DioVaultEntry {
    wchar_t name[DIO_VAULT_NAME_CAP];
    wchar_t *value;
    bool excluded_from_wer;
} DioVaultEntry;

typedef struct DioVault {
    wchar_t path[MAX_PATH];
    unsigned char salt[16];
    unsigned char key[32];
    DioVaultEntry entries[DIO_VAULT_MAX_ENTRIES];
    size_t entry_count;
    bool unlocked;
    bool key_excluded_from_wer;
} DioVault;

void dio_vault_init(DioVault *vault, const wchar_t *path);
void dio_vault_lock(DioVault *vault);
bool dio_vault_exists(const DioVault *vault);
bool dio_vault_create(
    DioVault *vault,
    const wchar_t *master_password,
    wchar_t *error,
    size_t error_capacity);
bool dio_vault_unlock(
    DioVault *vault,
    const wchar_t *master_password,
    wchar_t *error,
    size_t error_capacity);
bool dio_vault_change_password(
    DioVault *vault,
    const wchar_t *new_master_password,
    wchar_t *error,
    size_t error_capacity);
bool dio_vault_reset(
    DioVault *vault,
    wchar_t *error,
    size_t error_capacity);
const wchar_t *dio_vault_get(
    const DioVault *vault,
    const wchar_t *name);
bool dio_vault_set(
    DioVault *vault,
    const wchar_t *name,
    const wchar_t *value,
    wchar_t *error,
    size_t error_capacity);
bool dio_vault_remove(
    DioVault *vault,
    const wchar_t *name,
    wchar_t *error,
    size_t error_capacity);
bool dio_vault_save(
    DioVault *vault,
    wchar_t *error,
    size_t error_capacity);

#endif
