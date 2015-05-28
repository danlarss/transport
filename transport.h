#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <curl/curl.h>

#define TRANSPORT_HOST_LEN 32
#define TRANSPORT_INDEX_LEN 32
#define TRANSPORT_CALL_URL_LEN 255

typedef struct {
	char * ptr;
	size_t len;
} str_t;

typedef struct {
	char host[TRANSPORT_HOST_LEN];
	short port;
	int timeout;
	CURL *curl;
	str_t response;
} transport_head_t;

typedef struct {
	transport_head_t * (* const create)(const char *, short, int);
	int (* const get)(transport_head_t *, const char *);
	int (* const post)(transport_head_t *, const char *, const char *);
	int (* const put)(transport_head_t *, const char *, const char *);
	int (* const delete)(transport_head_t *, const char *, const char *);
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
	TRANS_ERROR_INPUT = 100,
	TRANS_ERROR_CURL
};

extern _transport_t const transport;

#endif
