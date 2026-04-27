#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/http/status.h>

#include "config.h"
#include "http_server.h"
#include "lock_ctrl.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(http_srv, LOG_LEVEL_INF);

#define HTTP_THREAD_STACK_SIZE 4096
#define HTTP_THREAD_PRIORITY   7

static K_THREAD_STACK_DEFINE(http_thread_stack, HTTP_THREAD_STACK_SIZE);
static struct k_thread http_thread_data;

#define POST_BUF_SIZE 256

static const uint8_t index_html[] = {
#include "index_html.inc"
};

static const char config_html[] =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<title>SmartLock Setup</title></head><body>"
	"<h1>WiFi Setup</h1>"
	"<form method=\"POST\" action=\"/config\">"
	"<p><label>SSID: <input name=\"ssid\" maxlength=\"32\" required></label></p>"
	"<p><label>Password: <input name=\"password\" type=\"password\" maxlength=\"64\"></label></p>"
	"<button type=\"submit\">Save & Reboot</button>"
	"</form></body></html>";

static const char unlock_ok_body[]   = "OK";
static const char unlock_bad_body[]  = "Invalid PIN";
static const char config_ok_body[]   = "Saved. Rebooting...";
static const char not_found_body[]   = "Not found";
static const char bad_request_body[] = "Bad request";

static int find_form_field(const char *body, size_t body_len,
			   const char *key, char *out, size_t out_size)
{
	size_t key_len = strlen(key);
	const char *p = body;
	const char *end = body + body_len;

	while (p < end) {
		const char *amp = memchr(p, '&', end - p);
		const char *seg_end = amp ? amp : end;

		if ((size_t)(seg_end - p) > key_len &&
		    p[key_len] == '=' &&
		    memcmp(p, key, key_len) == 0) {
			const char *vstart = p + key_len + 1;
			size_t vlen = seg_end - vstart;

			if (vlen >= out_size) {
				return -ENOMEM;
			}
			memcpy(out, vstart, vlen);
			out[vlen] = '\0';
			return (int)vlen;
		}

		if (!amp) {
			break;
		}
		p = amp + 1;
	}

	return -ENOENT;
}

static int collect_body(enum http_transaction_status status,
			const struct http_request_ctx *req,
			uint8_t *buf, size_t buf_size, size_t *cursor)
{
	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		*cursor = 0;
		return -EAGAIN;
	}

	if (req->data_len + *cursor > buf_size) {
		*cursor = 0;
		return -ENOMEM;
	}

	memcpy(buf + *cursor, req->data, req->data_len);
	*cursor += req->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	return -EINPROGRESS;
}

static void set_text_response(struct http_response_ctx *resp,
			      enum http_status status,
			      const char *body, size_t body_len)
{
	static const struct http_header text_headers[] = {
		{.name = "Content-Type", .value = "text/plain"},
	};

	resp->status = status;
	resp->headers = text_headers;
	resp->header_count = ARRAY_SIZE(text_headers);
	resp->body = (const uint8_t *)body;
	resp->body_len = body_len;
	resp->final_chunk = true;
}

static int index_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx,
			 void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static const struct http_header html_headers[] = {
		{.name = "Content-Type", .value = "text/html"},
	};

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = html_headers;
	response_ctx->header_count = ARRAY_SIZE(html_headers);
	response_ctx->body = index_html;
	response_ctx->body_len = sizeof(index_html);
	response_ctx->final_chunk = true;
	return 0;
}

static int status_handler(struct http_client_ctx *client,
			  enum http_transaction_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static char body[160];
	const char *ip = wifi_manager_get_ip();
	int n;

	if (ip[0]) {
		n = snprintf(body, sizeof(body),
			     "{\"locked\":%s,\"wifi\":\"%s\",\"ip\":\"%s\","
			     "\"temp\":null,\"humidity\":null}",
			     lock_ctrl_is_locked() ? "true" : "false",
			     wifi_manager_get_state(), ip);
	} else {
		n = snprintf(body, sizeof(body),
			     "{\"locked\":%s,\"wifi\":\"%s\",\"ip\":null,"
			     "\"temp\":null,\"humidity\":null}",
			     lock_ctrl_is_locked() ? "true" : "false",
			     wifi_manager_get_state());
	}

	if (n < 0 || n >= (int)sizeof(body)) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	static const struct http_header json_headers[] = {
		{.name = "Content-Type", .value = "application/json"},
		{.name = "Cache-Control", .value = "no-store"},
	};

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_headers;
	response_ctx->header_count = ARRAY_SIZE(json_headers);
	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = n;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = status_handler,
};

static struct http_resource_detail_dynamic index_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = index_handler,
};

static int unlock_handler(struct http_client_ctx *client,
			  enum http_transaction_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static uint8_t buf[POST_BUF_SIZE];
	static size_t cursor;

	int rc = collect_body(status, request_ctx, buf, sizeof(buf), &cursor);

	if (rc == -EINPROGRESS) {
		return 0;
	}
	if (rc == -EAGAIN) {
		return 0;
	}
	if (rc == -ENOMEM) {
		set_text_response(response_ctx, HTTP_400_BAD_REQUEST,
				  bad_request_body, sizeof(bad_request_body) - 1);
		return 0;
	}

	char pin[16];
	int got = find_form_field((const char *)buf, cursor, "pin",
				  pin, sizeof(pin));
	cursor = 0;

	if (got < 0) {
		set_text_response(response_ctx, HTTP_400_BAD_REQUEST,
				  bad_request_body, sizeof(bad_request_body) - 1);
		return 0;
	}

	if (strcmp(pin, LOCK_PIN_CODE) == 0) {
		LOG_INF("PIN OK -> unlock");
		lock_ctrl_unlock();
		set_text_response(response_ctx, HTTP_200_OK,
				  unlock_ok_body, sizeof(unlock_ok_body) - 1);
	} else {
		LOG_WRN("PIN rejected");
		set_text_response(response_ctx, HTTP_403_FORBIDDEN,
				  unlock_bad_body, sizeof(unlock_bad_body) - 1);
	}
	return 0;
}

static struct http_resource_detail_dynamic unlock_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = unlock_handler,
};

static int config_handler(struct http_client_ctx *client,
			  enum http_transaction_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	static uint8_t buf[POST_BUF_SIZE];
	static size_t cursor;

	if (wifi_manager_mode() != WIFI_MGR_MODE_AP) {
		if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
			return 0;
		}
		set_text_response(response_ctx, HTTP_404_NOT_FOUND,
				  not_found_body, sizeof(not_found_body) - 1);
		return 0;
	}

	if (client->method == HTTP_GET) {
		if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
			return 0;
		}
		static const struct http_header html_headers[] = {
			{.name = "Content-Type", .value = "text/html"},
		};
		response_ctx->status = HTTP_200_OK;
		response_ctx->headers = html_headers;
		response_ctx->header_count = ARRAY_SIZE(html_headers);
		response_ctx->body = (const uint8_t *)config_html;
		response_ctx->body_len = sizeof(config_html) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}

	int rc = collect_body(status, request_ctx, buf, sizeof(buf), &cursor);

	if (rc == -EINPROGRESS || rc == -EAGAIN) {
		return 0;
	}
	if (rc == -ENOMEM) {
		set_text_response(response_ctx, HTTP_400_BAD_REQUEST,
				  bad_request_body, sizeof(bad_request_body) - 1);
		return 0;
	}

	char ssid[WIFI_SSID_MAX_LEN + 1];
	char psk[WIFI_PSK_MAX_LEN + 1];

	int s = find_form_field((const char *)buf, cursor, "ssid",
				ssid, sizeof(ssid));
	int p = find_form_field((const char *)buf, cursor, "password",
				psk, sizeof(psk));
	cursor = 0;

	if (s <= 0) {
		set_text_response(response_ctx, HTTP_400_BAD_REQUEST,
				  bad_request_body, sizeof(bad_request_body) - 1);
		return 0;
	}
	if (p < 0) {
		psk[0] = '\0';
	}

	if (wifi_manager_save_credentials(ssid, psk) < 0) {
		set_text_response(response_ctx, HTTP_400_BAD_REQUEST,
				  bad_request_body, sizeof(bad_request_body) - 1);
		return 0;
	}

	LOG_INF("Credentials saved, scheduling reboot");
	wifi_manager_schedule_reboot(1000);

	set_text_response(response_ctx, HTTP_200_OK,
			  config_ok_body, sizeof(config_ok_body) - 1);
	return 0;
}

static struct http_resource_detail_dynamic config_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
	},
	.cb = config_handler,
};

static uint16_t http_service_port = HTTP_SERVER_PORT;
HTTP_SERVICE_DEFINE(elock_http_service, NULL, &http_service_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(elock_index, elock_http_service, "/", &index_detail);
HTTP_RESOURCE_DEFINE(elock_status, elock_http_service, "/status", &status_detail);
HTTP_RESOURCE_DEFINE(elock_unlock, elock_http_service, "/unlock", &unlock_detail);
HTTP_RESOURCE_DEFINE(elock_config, elock_http_service, "/config", &config_detail);

static void http_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (!wifi_manager_network_ready()) {
		k_msleep(200);
	}

	int ret = http_server_start();

	if (ret < 0) {
		LOG_ERR("http_server_start failed: %d", ret);
		return;
	}
	LOG_INF("HTTP server started on :%u", HTTP_SERVER_PORT);
}

int http_server_thread_init(void)
{
	k_thread_create(&http_thread_data, http_thread_stack,
			K_THREAD_STACK_SIZEOF(http_thread_stack),
			http_thread_entry, NULL, NULL, NULL,
			HTTP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&http_thread_data, "http_thread");
	return 0;
}
