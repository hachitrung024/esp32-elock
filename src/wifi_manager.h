#ifndef ELOCK_WIFI_MANAGER_H_
#define ELOCK_WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>

/*
 * WiFi state machine: BOOT -> (load NVS) -> STA_CONNECT or AP_MODE,
 * with AP_MODE fallback after WIFI_RETRY_COUNT failures. Runs on its
 * own wifi_thread.
 */

enum wifi_mgr_mode {
	WIFI_MGR_MODE_BOOT,
	WIFI_MGR_MODE_CONNECTING,
	WIFI_MGR_MODE_AP,
	WIFI_MGR_MODE_STA,
};

int  wifi_manager_init(void);
enum wifi_mgr_mode wifi_manager_mode(void);
bool wifi_manager_network_ready(void);

/* Lowercase status string per TASK-002: "sta" / "ap" / "connecting" / "boot". */
const char *wifi_manager_get_state(void);

/*
 * Current IPv4 address as a NUL-terminated string ("192.168.4.1" in AP mode,
 * DHCP-assigned in STA, "" if not yet known).
 */
const char *wifi_manager_get_ip(void);

/*
 * Persist WiFi credentials to Settings. Returns 0 on success.
 * Both ssid and psk are NUL-terminated strings.
 */
int  wifi_manager_save_credentials(const char *ssid, const char *psk);

/*
 * Schedule a cold reboot after delay_ms via the system work queue.
 */
void wifi_manager_schedule_reboot(int delay_ms);

#endif /* ELOCK_WIFI_MANAGER_H_ */
