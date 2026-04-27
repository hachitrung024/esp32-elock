#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "lock_ctrl.h"

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

#define BTN_NODE DT_CHILD(DT_ALIAS(btn_lock), btn_pin)
BUILD_ASSERT(DT_NODE_EXISTS(BTN_NODE), "btn-lock alias / btn_pin child missing");

static const struct gpio_dt_spec btn_gpio = GPIO_DT_SPEC_GET(BTN_NODE, gpios);

#define BUTTON_THREAD_STACK_SIZE 1024
#define BUTTON_THREAD_PRIORITY   3
#define BUTTON_POLL_MS           10
#define BUTTON_DEBOUNCE_MS       50

static K_THREAD_STACK_DEFINE(button_thread_stack, BUTTON_THREAD_STACK_SIZE);
static struct k_thread button_thread_data;

static void button_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	bool last_stable = false;
	bool candidate = false;
	int64_t candidate_since = 0;

	while (1) {
		int raw = gpio_pin_get_dt(&btn_gpio);

		if (raw < 0) {
			k_msleep(BUTTON_POLL_MS);
			continue;
		}

		bool pressed = raw != 0;

		if (pressed != candidate) {
			candidate = pressed;
			candidate_since = k_uptime_get();
		} else if (candidate != last_stable &&
			   (k_uptime_get() - candidate_since) >= BUTTON_DEBOUNCE_MS) {
			last_stable = candidate;
			if (last_stable) {
				LOG_INF("Button pressed -> lock");
				lock_ctrl_lock();
			}
		}

		k_msleep(BUTTON_POLL_MS);
	}
}

int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&btn_gpio)) {
		LOG_ERR("Button GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&btn_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Configure button GPIO failed: %d", ret);
		return ret;
	}

	k_thread_create(&button_thread_data, button_thread_stack,
			K_THREAD_STACK_SIZEOF(button_thread_stack),
			button_thread_entry, NULL, NULL, NULL,
			BUTTON_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&button_thread_data, "button_thread");

	return 0;
}
