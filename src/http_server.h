#ifndef ELOCK_HTTP_SERVER_H_
#define ELOCK_HTTP_SERVER_H_

/*
 * Spawns http_thread which waits for the network to become ready and then
 * starts the Zephyr HTTP server. Routes are registered statically via
 * HTTP_RESOURCE_DEFINE in http_server.c.
 */

int http_server_thread_init(void);

#endif /* ELOCK_HTTP_SERVER_H_ */
