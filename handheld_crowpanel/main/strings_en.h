#ifndef _STRINGS_EN_H_
#define _STRINGS_EN_H_

// English strings — all user-visible text for the handheld UI

// App
#define STR_APP_NAME            "Pilocows"

// Screens
#define STR_SCREEN_SCAN         "Scan"
#define STR_SCREEN_HISTORY      "History"
#define STR_SCREEN_SETTINGS     "Settings"

// Scan screen
#define STR_SCAN_READY          "Ready to scan"
#define STR_SCAN_SCANNING       "Scanning..."
#define STR_SCAN_TAG_FOUND      "Tag found"
#define STR_SCAN_EID_LABEL      "EID:"
#define STR_SCAN_EVENT_LABEL    "Event:"
#define STR_SCAN_COUNT_LABEL    "Scanned:"
#define STR_SCAN_CLEAR          "Clear list"
#define STR_SCAN_SYNC           "Sync to PC"

// Event types
#define STR_EVENT_GENERAL       "General"
#define STR_EVENT_WEIGHING      "Weighing"
#define STR_EVENT_VACCINATION   "Vaccination"
#define STR_EVENT_PREGNANCY     "Pregnancy Check"
#define STR_EVENT_TEST          "Test"
#define STR_EVENT_REMOVAL       "Removal"

// History screen
#define STR_HISTORY_EMPTY       "No scans yet"
#define STR_HISTORY_TITLE       "Scan History"

// Date & time field labels (modal spinboxes)
#define STR_DATETIME_YEAR       "Year"
#define STR_DATETIME_MONTH      "Mon"
#define STR_DATETIME_DAY        "Day"
#define STR_DATETIME_HOUR       "Hour"
#define STR_DATETIME_MIN        "Min"
#define STR_DATETIME_TIMEZONE   "Timezone"

// Settings screen
#define STR_SETTINGS_TITLE      "Settings"
#define STR_SETTINGS_LANGUAGE   "Language"
#define STR_SETTINGS_VIBRATOR   "Vibrator"
#define STR_SETTINGS_BRIGHTNESS "Brightness"
#define STR_SETTINGS_VOLUME     "Volume"
#define STR_SETTINGS_TEST_SOUNDS "Test Sounds"
#define STR_SETTINGS_DATETIME   "Date & Time"
#define STR_SETTINGS_SET_TIME   "Set Time"
#define STR_SETTINGS_ABOUT      "About"
#define STR_SETTINGS_VERSION    "Version"
#define STR_SETTINGS_SAVE       "Save"

// WiFi
#define STR_SETTINGS_WIFI       "WiFi"
#define STR_WIFI_NETWORK        "Network"
#define STR_WIFI_PASSWORD       "Password"
#define STR_WIFI_CONNECT        "Connect"
#define STR_WIFI_CONFIGURE      "Configure"
#define STR_WIFI_RESCAN         "Rescan"
#define STR_WIFI_CONNECTED      "Connected"
#define STR_WIFI_DISCONNECTED   "Disconnected"
#define STR_WIFI_SEARCHING      "Searching..."
#define STR_WIFI_NO_NETWORKS    "(none found)"
#define STR_WIFI_WRONG_PASS     "Wrong password"
#define STR_WIFI_CONNECTING     "Connecting..."

// BLE / Sync
#define STR_SETTINGS_SYNC       "Sync to PC"
#define STR_BLE_SYNCED          "Synced"
#define STR_BLE_ADVERTISING     "Waiting for PC..."
#define STR_BLE_CONNECTED       "PC connected"
#define STR_BLE_SYNCING         "Syncing..."
#define STR_BLE_SYNC_DONE       "Sync complete"
#define STR_BLE_SYNC_FAILED     "Sync failed"

// Mic recording test + gain tuning (single combined screen)
#define STR_SETTINGS_MIC_TEST      "Microphone"
#define STR_MIC_TEST_TITLE         "Microphone"
#define STR_MIC_HOLD_TO_RECORD     "Hold to record"
#define STR_MIC_RECORDING          "Recording..."
#define STR_MIC_RECORDED           "Recorded"
#define STR_MIC_UNAVAILABLE        "Microphone unavailable"
#define STR_MIC_GAIN_LABEL         "Gain"

// Audio note recorder (reusable screen)
#define STR_AUDIO_NOTE_HAS_RECORDING "Voice note recorded"
#define STR_AUDIO_NOTE_NONE          "No voice note"

// Sessions — menu / list
#define STR_SESSION_TITLE           "Sessions"
#define STR_SESSION_NEW             "New Session"
#define STR_SESSION_EDIT            "Edit Session"
#define STR_SESSION_RESUME          "Resume Session"
#define STR_SESSION_LIST            "Session List"
#define STR_SESSION_NONE            "No active session"
#define STR_SESSION_ACTIVE          "Active:"
#define STR_SESSION_ANIMALS         "Animals:"
#define STR_SESSION_DELETE          "Delete"
#define STR_SESSION_SET_CURRENT     "Set as Current"
#define STR_SESSION_CONFIRM_DELETE  "Delete this session?"
#define STR_SESSION_DISCARD         "Discard"
#define STR_SESSION_CONFIRM_DISCARD "Discard this new session?"
#define STR_SESSION_CONFIRM_DISCARD_EDIT "Discard these changes?"

// Sessions — create
#define STR_SESSION_TYPE            "Type"
#define STR_SESSION_NAME            "Name"
#define STR_SESSION_SELECT_VAX      "Vaccines"
#define STR_SESSION_CREATE          "Create"

// Scan screen — session-aware
#define STR_SCAN_NO_SESSION         "No active session"
#define STR_SCAN_START_SESSION      "Start a session first"
#define STR_SCAN_DUPLICATE          "Already scanned"
#define STR_SCAN_NEW_TAG            "New animal"
#define STR_SCAN_SESSION            "Session:"

// Weighing
#define STR_WEIGHT_KG               "Weight (kg)"
#define STR_WEIGHT_LABEL            "Weight:"

// Pregnancy
#define STR_PREG_RESULT             "Pregnancy"
#define STR_PREG_UNKNOWN            "Unknown"
#define STR_PREG_NO                 "Not pregnant"
#define STR_PREG_SMALL              "Small"
#define STR_PREG_MEDIUM             "Medium"
#define STR_PREG_BIG                "Big"
#define STR_PREG_REJECTED           "Rejected"

// Test (generic configurable test)
#define STR_TEST_RESULT             "Test"
#define STR_TEST_POSITIVE           "Positive"
#define STR_TEST_NEGATIVE           "Negative"
#define STR_TEST_INCONCLUSIVE       "Inconclusive"

// Notes
#define STR_NOTE                    "Note"
#define STR_NOTE_PLACEHOLDER        "Add animal note..."
#define STR_SESSION_NOTE            "Session note"
#define STR_SESSION_NOTE_PLACEHOLDER "Add session note..."
#define STR_SESSION_VOICE_NOTE      "Voice Note"
#define STR_EDIT_SESSION_NOTE       "Edit session note"

// Vaccines settings
#define STR_SETTINGS_VACCINES       "Vaccines"
#define STR_VACCINES_TITLE          "Vaccines"
#define STR_VACCINE_ADD             "Add Vaccine"
#define STR_VACCINE_NAME            "Vaccine Name"
#define STR_VACCINE_DELETE          "Delete"
#define STR_VACCINE_NONE            "No vaccines configured"
#define STR_VACCINE_CONFIRM_DELETE  "Delete this vaccine?"

// Tests settings
#define STR_SETTINGS_TESTS          "Tests"
#define STR_TESTS_TITLE             "Tests"
#define STR_TEST_ADD                "Add Test"
#define STR_TEST_NAME               "Test Name"
#define STR_TEST_DELETE             "Delete"
#define STR_TEST_NONE               "No tests configured"
#define STR_TEST_CONFIRM_DELETE     "Delete this test?"
#define STR_SESSION_SELECT_TEST     "Test"

// Common
#define STR_BTN_OK              "OK"
#define STR_BTN_CANCEL          "Cancel"
#define STR_BTN_CLOSE           "Close"
#define STR_BTN_BACK            "Back"
#define STR_BTN_CONFIRM         "Confirm"
#define STR_ON                  "On"
#define STR_OFF                 "Off"

#endif
