#pragma once

#include <stdint.h>

typedef enum {
    LANG_EN = 0,
    LANG_ES = 1,
} language_t;

// Must be called after NVS init. Loads saved language preference.
void i18n_init(void);

// Change language and persist to NVS.
void i18n_set_language(language_t lang);

language_t i18n_get_language(void);

// Translate a string key. Returns the string for the current language.
// Keys are the English #defines from strings_en.h — pass the English literal.
// Usage: i18n_t(STR_SCAN_READY)  →  "Listo para escanear" (if ES)
const char *i18n_t(const char *en_key);
