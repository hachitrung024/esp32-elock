#ifndef ELOCK_CONFIG_H_
#define ELOCK_CONFIG_H_

#define LOCK_PIN_CODE           "123456"
#define LOCK_UNLOCK_DURATION_S  5

#define WIFI_RETRY_COUNT        5
#define WIFI_RETRY_DELAY_MS     3000
#define WIFI_AP_SSID            "SmartLock-Setup"
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_IP              "192.168.4.1"
#define WIFI_AP_NETMASK         "255.255.255.0"

#define HTTP_SERVER_PORT        80

#define SETTINGS_KEY_WIFI_SSID  "elock/wifi/ssid"
#define SETTINGS_KEY_WIFI_PSK   "elock/wifi/psk"

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PSK_MAX_LEN        64

#endif /* ELOCK_CONFIG_H_ */
