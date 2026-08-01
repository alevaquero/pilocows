#ifndef _OTA_SERVER_H_
#define _OTA_SERVER_H_

// HTTP OTA server — call ota_server_start() once WiFi has an IP address.
// Serves:
//   GET  /        — browser upload form (HTML, no dependencies)
//   POST /update  — raw firmware binary (Content-Type: application/octet-stream)
//
// On a successful upload the device sets the new boot partition and reboots.
void ota_server_start(void);

#endif
