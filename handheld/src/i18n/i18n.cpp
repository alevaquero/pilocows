#include "i18n.h"
// Do NOT include both strings_en.h and strings_es.h here — they define the
// same macro names causing redefinition warnings. The lookup table below uses
// string literals directly; the macros are only used in UI source files
// which include exactly one of the two headers (via i18n.h -> strings_en.h).
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "i18n";
static const char *NVS_NAMESPACE = "pilocows";
static const char *NVS_KEY_LANG  = "language";

static language_t s_current_lang = LANG_ES;  // Default: Spanish

// Parallel string tables — must match order of macros in strings_*.h
// This approach maps English strings to Spanish at runtime.
// Each entry: { english, spanish }
static const struct { const char *en; const char *es; } s_table[] = {
    { "Pilocows",                   "Pilocows" },
    { "Scan",                       "Escanear" },
    { "History",                    "Historial" },
    { "Settings",                   "Ajustes" },
    { "Ready to scan",              "Listo para escanear" },
    { "Scanning...",                "Escaneando..." },
    { "Tag found",                  "Caravana detectada" },
    { "EID:",                       "EID:" },
    { "Event:",                     "Evento:" },
    { "Scanned:",                   "Escaneados:" },
    { "Clear list",                 "Borrar lista" },
    { "Sync to PC",                 "Sincronizar" },
    { "General",                    "General" },
    { "Weighing",                   "Pesaje" },
    { "Vaccination",                "Vacunacion" },
    { "Pregnancy Check",            "Control de prenez" },
    { "TB Test",                    "Test de tuberculosis" },
    { "Removal",                    "Baja" },
    { "No scans yet",               "Sin escaneos" },
    { "Scan History",               "Historial de escaneos" },
    { "Language",                   "Idioma" },
    { "Buzzer",                     "Buzzer" },
    { "Vibrator",                   "Vibrador" },
    { "Brightness",                 "Brillo" },
    { "Date & Time",                "Fecha y hora" },
    { "Set Time",                   "Ajustar hora" },
    { "About",                      "Acerca de" },
    { "Version",                    "Version" },
    { "Save",                       "Guardar" },
    { "Year",                       "Anno" },
    { "Mon",                        "Mes" },
    { "Day",                        "Dia" },
    { "Hour",                       "Hora" },
    { "Min",                        "Min" },
    { "WiFi",                        "WiFi" },
    { "Network",                     "Red" },
    { "Password",                    "Contrasena" },
    { "Connect",                     "Conectar" },
    { "Configure",                   "Configurar" },
    { "Rescan",                      "Buscar" },
    { "Connected",                   "Conectado" },
    { "Disconnected",                "Desconectado" },
    { "Searching...",                "Buscando..." },
    { "(none found)",                "(ninguna)" },
    { "Wrong password",              "Contrasena incorrecta" },
    { "Connecting...",               "Conectando..." },
    { "Waiting for PC...",          "Esperando PC..." },
    { "PC connected",               "PC conectada" },
    { "Syncing...",                 "Sincronizando..." },
    { "Sync complete",              "Sincronizacion completa" },
    { "Sync failed",                "Error de sincronizacion" },
    // Sessions — menu / list
    { "Sessions",                   "Sesiones" },
    { "New Session",                "Nueva sesion" },
    { "Resume Session",             "Continuar sesion" },
    { "Session List",               "Lista de sesiones" },
    { "No active session",          "Sin sesion activa" },
    { "Active:",                    "Activa:" },
    { "Animals:",                   "Animales:" },
    { "Delete",                     "Eliminar" },
    { "Set as Current",             "Activar" },
    { "Delete this session?",       "Eliminar esta sesion?" },
    // Sessions — create
    { "Type",                        "Tipo" },
    { "Name",                        "Nombre" },
    { "Vaccines",                    "Vacunas" },
    { "Create",                     "Crear" },
    { "Please select at least one vaccine.", "Selecciona al menos una vacuna." },
    // Scan screen — session-aware
    { "Start a session first",      "Inicia una sesion primero" },
    { "Already scanned",            "Ya escaneado" },
    { "New animal",                 "Animal nuevo" },
    { "Session:",                   "Sesion:" },
    // Weighing
    { "Weight (kg)",                "Peso (kg)" },
    { "Weight:",                    "Peso:" },
    // Pregnancy
    { "Pregnancy",                  "Prenez" },
    { "Unknown",                    "Desconocido" },
    { "Not pregnant",               "No gestante" },
    { "Small",                      "Pequeña" },
    { "Medium",                     "Mediana" },
    { "Big",                        "Grande" },
    { "Rejected",                   "Descarte" },
    // TB Test
    { "TB Test",                    "Test tuberculosis" },
    { "Positive",                   "Positivo" },
    { "Negative",                   "Negativo" },
    { "Inconclusive",               "No concluyente" },
    // Notes
    { "Note",                       "Nota" },
    { "Add animal note...",          "Agregar nota del animal..." },
    { "Session note",               "Nota de sesion" },
    { "Add session note...",        "Agregar nota de sesion..." },
    { "Edit session note",         "Editar nota de sesion" },
    // Vaccines settings
    { "Vaccines",                   "Vacunas" },
    { "Add Vaccine",                "Agregar vacuna" },
    { "Vaccine Name",               "Nombre de vacuna" },
    { "No vaccines configured",     "Sin vacunas configuradas" },
    { "Delete this vaccine?",       "Eliminar esta vacuna?" },
    // BLE / Sync
    { "Sync to PC",                  "Sincronizar con PC" },
    { "Synced",                      "Sincronizada" },
    // Common
    { "OK",                         "OK" },
    { "Cancel",                     "Cancelar" },
    { "Close",                      "Cerrar" },
    { "Back",                       "Atras" },
    { "Confirm",                    "Confirmar" },
    { "On",                         "Si" },
    { "Off",                        "No" },
};

#define TABLE_SIZE (sizeof(s_table) / sizeof(s_table[0]))

void i18n_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t val = (uint8_t)LANG_ES;
        nvs_get_u8(handle, NVS_KEY_LANG, &val);
        s_current_lang = (language_t)val;
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Language: %s", s_current_lang == LANG_EN ? "EN" : "ES");
}

void i18n_set_language(language_t lang)
{
    s_current_lang = lang;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_LANG, (uint8_t)lang);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

language_t i18n_get_language(void)
{
    return s_current_lang;
}

const char *i18n_t(const char *en_key)
{
    if (s_current_lang == LANG_EN) {
        return en_key;
    }
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        if (strcmp(s_table[i].en, en_key) == 0) {
            return s_table[i].es;
        }
    }
    // Key not found — return English as fallback
    ESP_LOGW(TAG, "Missing ES translation for: %s", en_key);
    return en_key;
}
