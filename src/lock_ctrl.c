#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "config.h"
#include "lock_ctrl.h"

LOG_MODULE_REGISTER(lock_ctrl, LOG_LEVEL_INF);

#define LOCK_NODE DT_CHILD(DT_ALIAS(lock_out), lock_pin)
BUILD_ASSERT(DT_NODE_EXISTS(LOCK_NODE), "lock-out alias / lock_pin child missing");

static const struct gpio_dt_spec lock_gpio = GPIO_DT_SPEC_GET(LOCK_NODE, gpios);

static K_SEM_DEFINE(unlock_request_sem, 0, 1);
static K_SEM_DEFINE(lock_now_sem, 0, 1);

static atomic_t is_unlocked = ATOMIC_INIT(0);

#define LOCK_THREAD_STACK_SIZE 1024
#define LOCK_THREAD_PRIORITY   4

static K_THREAD_STACK_DEFINE(lock_thread_stack, LOCK_THREAD_STACK_SIZE);
static struct k_thread lock_thread_data;

static void lock_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_sem_take(&unlock_request_sem, K_FOREVER);

		k_sem_reset(&lock_now_sem);
		gpio_pin_set_dt(&lock_gpio, 1);
		atomic_set(&is_unlocked, 1);
		LOG_INF("Lock: UNLOCKED");

		(void)k_sem_take(&lock_now_sem, K_SECONDS(LOCK_UNLOCK_DURATION_S));

		gpio_pin_set_dt(&lock_gpio, 0);
		atomic_set(&is_unlocked, 0);
		LOG_INF("Lock: LOCKED");
	}
}

int lock_ctrl_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&lock_gpio)) {
		LOG_ERR("Lock GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&lock_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Configure lock GPIO failed: %d", ret);
		return ret;
	}

	k_thread_create(&lock_thread_data, lock_thread_stack,
			K_THREAD_STACK_SIZEOF(lock_thread_stack),
			lock_thread_entry, NULL, NULL, NULL,
			LOCK_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&lock_thread_data, "lock_thread");

	return 0;
}

void lock_ctrl_unlock(void)
{
	k_sem_give(&unlock_request_sem);
}

void lock_ctrl_lock(void)
{
	gpio_pin_set_dt(&lock_gpio, 0);
	atomic_set(&is_unlocked, 0);
	k_sem_give(&lock_now_sem);
}

bool lock_ctrl_is_locked(void)
{
	return atomic_get(&is_unlocked) == 0;
}
