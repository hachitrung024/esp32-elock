#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include "config.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define WIFI_THREAD_STACK_SIZE 4096
#define WIFI_THREAD_PRIORITY   5

#define WIFI_EVENT_MASK                                                                            \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT)

#define IPV4_EVENT_MASK (NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL)

static K_THREAD_STACK_DEFINE(wifi_thread_stack, WIFI_THREAD_STACK_SIZE);
static struct k_thread wifi_thread_data;

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static char ip_str[NET_IPV4_ADDR_LEN];

static char saved_ssid[WIFI_SSID_MAX_LEN + 1];
static char saved_psk[WIFI_PSK_MAX_LEN + 1];
static size_t saved_ssid_len;
static size_t saved_psk_len;

static enum wifi_mgr_mode current_mode = WIFI_MGR_MODE_BOOT;
static bool network_ready;

static K_SEM_DEFINE(wifi_connect_sem, 0, 1);
static int last_connect_status = -1;

static void reboot_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static int wifi_settings_set(const char *name, size_t len,
			     settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	ssize_t rc;

	if (settings_name_steq(name, "ssid", &next) && !next) {
		if (len > WIFI_SSID_MAX_LEN) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, saved_ssid, len);
		if (rc < 0) {
			return rc;
		}
		saved_ssid[rc] = '\0';
		saved_ssid_len = rc;
		return 0;
	}

	if (settings_name_steq(name, "psk", &next) && !next) {
		if (len > WIFI_PSK_MAX_LEN) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, saved_psk, len);
		if (rc < 0) {
			return rc;
		}
		saved_psk[rc] = '\0';
		saved_psk_len = rc;
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(elock_wifi, "elock/wifi", NULL,
			       wifi_settings_set, NULL, NULL);

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		last_connect_status = status->status;
		k_sem_give(&wifi_connect_sem);
		if (status->status == 0) {
			LOG_INF("STA connected");
			network_ready = true;
		} else {
			LOG_WRN("STA connect failed: %d", status->status);
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_INF("STA disconnected");
		break;
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		LOG_INF("AP enabled");
		network_ready = true;
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		LOG_INF("AP disabled");
		break;
	default:
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		const struct in_addr *addr = (const struct in_addr *)cb->info;

		if (addr) {
			net_addr_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
			LOG_INF("IPv4 addr: %s", ip_str);
		}
	} else if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		ip_str[0] = '\0';
	}
}

static int load_credentials(void)
{
	int rc = settings_load_subtree("elock/wifi");

	if (rc < 0) {
		LOG_WRN("settings_load_subtree(elock/wifi) failed: %d", rc);
		return rc;
	}
	return saved_ssid_len > 0 ? 0 : -ENOENT;
}

static int try_sta_connect(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct wifi_connect_req_params params = {0};

	if (!iface) {
		LOG_ERR("STA iface not available");
		return -ENODEV;
	}

	params.ssid = (const uint8_t *)saved_ssid;
	params.ssid_length = saved_ssid_len;
	if (saved_psk_len > 0) {
		params.psk = (const uint8_t *)saved_psk;
		params.psk_length = saved_psk_len;
		params.security = WIFI_SECURITY_TYPE_PSK;
	} else {
		params.security = WIFI_SECURITY_TYPE_NONE;
	}
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_OPTIONAL;

	current_mode = WIFI_MGR_MODE_CONNECTING;

	for (int attempt = 1; attempt <= WIFI_RETRY_COUNT; attempt++) {
		LOG_INF("STA connect attempt %d/%d to '%s'", attempt,
			WIFI_RETRY_COUNT, saved_ssid);

		k_sem_reset(&wifi_connect_sem);
		last_connect_status = -1;

		int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
				   sizeof(params));
		if (ret) {
			LOG_WRN("NET_REQUEST_WIFI_CONNECT failed: %d", ret);
		} else if (k_sem_take(&wifi_connect_sem,
				      K_MSEC(WIFI_RETRY_DELAY_MS * 4)) == 0 &&
			   last_connect_status == 0) {
			current_mode = WIFI_MGR_MODE_STA;
			return 0;
		}

		k_msleep(WIFI_RETRY_DELAY_MS);
	}

	return -ETIMEDOUT;
}

static int enable_ap_mode(void)
{
	struct net_if *iface = net_if_get_wifi_sap();
	struct wifi_connect_req_params params = {0};
	struct in_addr ap_addr;
	struct in_addr ap_netmask;
	struct in_addr dhcp_base;
	int ret;

	if (!iface) {
		LOG_ERR("AP iface not available");
		return -ENODEV;
	}

	if (net_addr_pton(AF_INET, WIFI_AP_IP, &ap_addr) < 0 ||
	    net_addr_pton(AF_INET, WIFI_AP_NETMASK, &ap_netmask) < 0) {
		LOG_ERR("Invalid AP IP/netmask");
		return -EINVAL;
	}

	net_if_ipv4_set_gw(iface, &ap_addr);
	if (!net_if_ipv4_addr_add(iface, &ap_addr, NET_ADDR_MANUAL, 0)) {
		LOG_WRN("Could not add AP IP");
	}
	if (!net_if_ipv4_set_netmask_by_addr(iface, &ap_addr, &ap_netmask)) {
		LOG_WRN("Could not set AP netmask");
	}

	dhcp_base = ap_addr;
	dhcp_base.s4_addr[3] += 10;
	if (net_dhcpv4_server_start(iface, &dhcp_base) != 0) {
		LOG_WRN("DHCPv4 server start failed");
	}

	params.ssid = (const uint8_t *)WIFI_AP_SSID;
	params.ssid_length = sizeof(WIFI_AP_SSID) - 1;
	params.psk_length = 0;
	params.security = WIFI_SECURITY_TYPE_NONE;
	params.channel = WIFI_AP_CHANNEL;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_DISABLE;

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &params, sizeof(params));
	if (ret) {
		LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
		return ret;
	}

	current_mode = WIFI_MGR_MODE_AP;
	strncpy(ip_str, WIFI_AP_IP, sizeof(ip_str) - 1);
	ip_str[sizeof(ip_str) - 1] = '\0';
	network_ready = true;
	return 0;
}

static void wifi_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int rc;

	rc = settings_subsys_init();
	if (rc < 0) {
		LOG_ERR("settings_subsys_init failed: %d", rc);
	} else {
		(void)load_credentials();
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, IPV4_EVENT_MASK);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (saved_ssid_len > 0) {
		LOG_INF("Found saved SSID, attempting STA");
		if (try_sta_connect() == 0) {
			return;
		}
		LOG_WRN("STA failed, falling back to AP");
	} else {
		LOG_INF("No saved credentials, starting AP");
	}

	if (enable_ap_mode() != 0) {
		LOG_ERR("AP mode failed to start");
	}
}

int wifi_manager_init(void)
{
	k_thread_create(&wifi_thread_data, wifi_thread_stack,
			K_THREAD_STACK_SIZEOF(wifi_thread_stack),
			wifi_thread_entry, NULL, NULL, NULL,
			WIFI_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&wifi_thread_data, "wifi_thread");
	return 0;
}

enum wifi_mgr_mode wifi_manager_mode(void)
{
	return current_mode;
}

bool wifi_manager_network_ready(void)
{
	return network_ready;
}

const char *wifi_manager_get_state(void)
{
	switch (current_mode) {
	case WIFI_MGR_MODE_STA:
		return "sta";
	case WIFI_MGR_MODE_AP:
		return "ap";
	case WIFI_MGR_MODE_CONNECTING:
		return "connecting";
	case WIFI_MGR_MODE_BOOT:
	default:
		return "boot";
	}
}

const char *wifi_manager_get_ip(void)
{
	return ip_str;
}

int wifi_manager_save_credentials(const char *ssid, const char *psk)
{
	int rc;

	if (!ssid) {
		return -EINVAL;
	}

	size_t ssid_len = strnlen(ssid, WIFI_SSID_MAX_LEN + 1);
	size_t psk_len = psk ? strnlen(psk, WIFI_PSK_MAX_LEN + 1) : 0;

	if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN ||
	    psk_len > WIFI_PSK_MAX_LEN) {
		return -EINVAL;
	}

	rc = settings_save_one(SETTINGS_KEY_WIFI_SSID, ssid, ssid_len);
	if (rc < 0) {
		LOG_ERR("save ssid failed: %d", rc);
		return rc;
	}

	rc = settings_save_one(SETTINGS_KEY_WIFI_PSK, psk ? psk : "", psk_len);
	if (rc < 0) {
		LOG_ERR("save psk failed: %d", rc);
		return rc;
	}

	return 0;
}

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("Rebooting now");
	sys_reboot(SYS_REBOOT_COLD);
}

void wifi_manager_schedule_reboot(int delay_ms)
{
	k_work_schedule(&reboot_work, K_MSEC(delay_ms));
}
