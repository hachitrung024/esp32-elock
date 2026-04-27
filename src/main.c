#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "http_server.h"
#include "lock_ctrl.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("SmartLock boot");

	if (lock_ctrl_init() < 0) {
		LOG_ERR("lock_ctrl_init failed");
	}
	if (button_init() < 0) {
		LOG_ERR("button_init failed");
	}
	if (wifi_manager_init() < 0) {
		LOG_ERR("wifi_manager_init failed");
	}
	if (http_server_thread_init() < 0) {
		LOG_ERR("http_server_thread_init failed");
	}

	return 0;
}
