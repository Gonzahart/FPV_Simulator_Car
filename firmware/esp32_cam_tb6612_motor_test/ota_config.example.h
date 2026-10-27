#pragma once

// Copy this file to ota_config.h in the same directory and choose a unique
// password. ota_config.h is gitignored so the real password is not published.
// HTTP Basic authentication sends credentials within the car's local Wi-Fi;
// keep the access point private and do not reuse an important password.
constexpr const char *OTA_USER = "admin";
constexpr const char *OTA_PASSWORD = "replace-with-a-long-unique-password";

#error "Set a unique OTA_PASSWORD, then remove this line from ota_config.h"
