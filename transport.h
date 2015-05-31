#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <curl/curl.h>

#define TRANSPORT_HOST_LEN 32
#define TRANSPORT_INDEX_LEN 32
#define TRANSPORT_CALL_URL_LEN 255
#define TRANSPORT_MAX_RESPONSE_BUFFER 65536
#define TRANSPORT_MAX_HOSTS 2

typedef struct {
	char * ptr;
	size_t size;
} str_t;

typedef struct {
	char host[TRANSPORT_HOST_LEN];
	int port;
} transport_host_t;

typedef struct {
	transport_host_t hosts[TRANSPORT_MAX_HOSTS];
	size_t num_hosts;
	int timeout;
	CURL *curl;
	str_t response;
} transport_head_t;

typedef struct {
	transport_head_t * (* const create)(const char *);
	int (* const search)(transport_head_t *, const char *, const char *, const char *);
	const char * (* const strerror)(int);
	void (* const destroy)(transport_head_t *);
} _transport_t;

enum {
	TRANS_METHOD_GET,
	TRANS_METHOD_POST,
	TRANS_METHOD_PUT,
	TRANS_METHOD_DELETE,
	TRANS_METHOD_MAX
};

enum {
	TRANS_ERROR_INPUT = 90,
	TRANS_ERROR_URL,
	TRANS_ERROR_CURL
};

extern _transport_t const transport;

#endif
