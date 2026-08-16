#ifndef _STRINGS_ES_H_
#define _STRINGS_ES_H_

// Spanish strings — all user-visible text for the handheld UI

// App
#define STR_APP_NAME            "Pilocows"

// Screens
#define STR_SCREEN_SCAN         "Escanear"
#define STR_SCREEN_HISTORY      "Historial"
#define STR_SCREEN_SETTINGS     "Ajustes"

// Scan screen
#define STR_SCAN_READY          "Listo para escanear"
#define STR_SCAN_SCANNING       "Escaneando..."
#define STR_SCAN_TAG_FOUND      "Caravana detectada"
#define STR_SCAN_EID_LABEL      "EID:"
#define STR_SCAN_EVENT_LABEL    "Evento:"
#define STR_SCAN_COUNT_LABEL    "Escaneados:"
#define STR_SCAN_CLEAR          "Borrar lista"
#define STR_SCAN_SYNC           "Sincronizar"

// Event types
#define STR_EVENT_GENERAL       "General"
#define STR_EVENT_WEIGHING      "Pesaje"
#define STR_EVENT_VACCINATION   "Vacunación"
#define STR_EVENT_PREGNANCY     "Control de preñez"
#define STR_EVENT_TEST          "Test"
#define STR_EVENT_REMOVAL       "Baja"

// History screen
#define STR_HISTORY_EMPTY       "Sin escaneos"
#define STR_HISTORY_TITLE       "Historial de escaneos"

// Date & time field labels (modal spinboxes)
#define STR_DATETIME_YEAR       "Año"
#define STR_DATETIME_MONTH      "Mes"
#define STR_DATETIME_DAY        "Día"
#define STR_DATETIME_HOUR       "Hora"
#define STR_DATETIME_MIN        "Min"
#define STR_DATETIME_TIMEZONE   "Zona horaria"

// Settings screen
#define STR_SETTINGS_TITLE      "Ajustes"
#define STR_SETTINGS_LANGUAGE   "Idioma"
#define STR_SETTINGS_VIBRATOR   "Vibrador"
#define STR_SETTINGS_BRIGHTNESS "Brillo"
#define STR_SETTINGS_VOLUME     "Volumen"
#define STR_SETTINGS_TEST_SOUNDS "Probar sonidos"
#define STR_SETTINGS_DATETIME   "Fecha y hora"
#define STR_SETTINGS_SET_TIME   "Ajustar hora"
#define STR_SETTINGS_ABOUT      "Acerca de"
#define STR_SETTINGS_VERSION    "Versión"
#define STR_SETTINGS_SAVE       "Guardar"

// WiFi
#define STR_SETTINGS_WIFI       "WiFi"
#define STR_WIFI_NETWORK        "Red"
#define STR_WIFI_PASSWORD       "Contraseña"
#define STR_WIFI_CONNECT        "Conectar"
#define STR_WIFI_CONFIGURE      "Configurar"
#define STR_WIFI_RESCAN         "Buscar"
#define STR_WIFI_CONNECTED      "Conectado"
#define STR_WIFI_DISCONNECTED   "Desconectado"
#define STR_WIFI_SEARCHING      "Buscando..."
#define STR_WIFI_NO_NETWORKS    "(ninguna)"
#define STR_WIFI_WRONG_PASS     "Contraseña incorrecta"
#define STR_WIFI_CONNECTING     "Conectando..."

// BLE / Sync
#define STR_SETTINGS_SYNC       "Sincronizar con PC"
#define STR_BLE_SYNCED          "Sincronizada"
#define STR_BLE_ADVERTISING     "Esperando PC..."
#define STR_BLE_CONNECTED       "PC conectada"
#define STR_BLE_SYNCING         "Sincronizando..."
#define STR_BLE_SYNC_DONE       "Sincronización completa"
#define STR_BLE_SYNC_FAILED     "Error de sincronización"

// Mic recording test + gain tuning (single combined screen)
#define STR_SETTINGS_MIC_TEST      "Micrófono"
#define STR_MIC_TEST_TITLE         "Micrófono"
#define STR_MIC_HOLD_TO_RECORD     "Mantén presionado para grabar"
#define STR_MIC_RECORDING          "Grabando..."
#define STR_MIC_RECORDED           "Grabado"
#define STR_MIC_UNAVAILABLE        "Micrófono no disponible"
#define STR_MIC_GAIN_LABEL         "Ganancia"

// Audio note recorder (reusable screen)
#define STR_AUDIO_NOTE_HAS_RECORDING "Nota de voz grabada"
#define STR_AUDIO_NOTE_NONE          "Sin nota de voz"

// Sessions — menu / list
#define STR_SESSION_TITLE           "Sesiones"
#define STR_SESSION_NEW             "Nueva sesión"
#define STR_SESSION_EDIT            "Editar sesión"
#define STR_SESSION_RESUME          "Continuar sesión"
#define STR_SESSION_LIST            "Lista de sesiones"
#define STR_SESSION_NONE            "Sin sesión activa"
#define STR_SESSION_ACTIVE          "Activa:"
#define STR_SESSION_ANIMALS         "Animales:"
#define STR_SESSION_DELETE          "Eliminar"
#define STR_SESSION_SET_CURRENT     "Activar"
#define STR_SESSION_CONFIRM_DELETE  "¿Eliminar esta sesión?"
#define STR_SESSION_DISCARD         "Descartar"
#define STR_SESSION_CONFIRM_DISCARD "¿Descartar esta nueva sesión?"
#define STR_SESSION_CONFIRM_DISCARD_EDIT "¿Descartar estos cambios?"

// Sessions — create
#define STR_SESSION_TYPE            "Tipo"
#define STR_SESSION_NAME            "Nombre"
#define STR_SESSION_SELECT_VAX      "Vacunas"
#define STR_SESSION_CREATE          "Crear"

// Scan screen — session-aware
#define STR_SCAN_NO_SESSION         "Sin sesión activa"
#define STR_SCAN_START_SESSION      "Inicia una sesión primero"
#define STR_SCAN_DUPLICATE          "Ya escaneado"
#define STR_SCAN_NEW_TAG            "Animal nuevo"
#define STR_SCAN_SESSION            "Sesión:"

// Weighing
#define STR_WEIGHT_KG               "Peso (kg)"
#define STR_WEIGHT_LABEL            "Peso:"

// Pregnancy
#define STR_PREG_RESULT             "Prenez"
#define STR_PREG_UNKNOWN            "Desconocido"
#define STR_PREG_NO                 "Vacía"
#define STR_PREG_SMALL              "Pequeña"
#define STR_PREG_MEDIUM             "Mediana"
#define STR_PREG_BIG                "Grande"
#define STR_PREG_REJECTED           "Descarte"

// Test (generic configurable test)
#define STR_TEST_RESULT             "Test"
#define STR_TEST_POSITIVE           "Positivo"
#define STR_TEST_NEGATIVE           "Negativo"
#define STR_TEST_INCONCLUSIVE       "No concluyente"

// Notes
#define STR_NOTE                    "Nota"
#define STR_NOTE_PLACEHOLDER        "Agregar nota del animal..."
#define STR_SESSION_NOTE            "Nota de sesión"
#define STR_SESSION_NOTE_PLACEHOLDER "Agregar nota de sesión..."
#define STR_SESSION_VOICE_NOTE      "Nota de voz"
#define STR_EDIT_SESSION_NOTE       "Editar nota de sesión"

// Vaccines settings
#define STR_SETTINGS_VACCINES       "Vacunas"
#define STR_VACCINES_TITLE          "Vacunas"
#define STR_VACCINE_ADD             "Agregar vacuna"
#define STR_VACCINE_NAME            "Nombre de vacuna"
#define STR_VACCINE_DELETE          "Eliminar"
#define STR_VACCINE_NONE            "Sin vacunas configuradas"
#define STR_VACCINE_CONFIRM_DELETE  "¿Eliminar esta vacuna?"

// Tests settings
#define STR_SETTINGS_TESTS          "Tests"
#define STR_TESTS_TITLE             "Tests"
#define STR_TEST_ADD                "Agregar test"
#define STR_TEST_NAME               "Nombre de test"
#define STR_TEST_DELETE             "Eliminar"
#define STR_TEST_NONE               "Sin tests configurados"
#define STR_TEST_CONFIRM_DELETE     "¿Eliminar este test?"
#define STR_SESSION_SELECT_TEST     "Test"

// Common
#define STR_BTN_OK              "OK"
#define STR_BTN_CANCEL          "Cancelar"
#define STR_BTN_CLOSE           "Cerrar"
#define STR_BTN_BACK            "Atrás"
#define STR_BTN_CONFIRM         "Confirmar"
#define STR_ON                  "Si"
#define STR_OFF                 "No"

#endif
