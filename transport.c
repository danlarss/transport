#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "transport.h"

static size_t
memorize_response(void *ptr, size_t size, size_t nmemb, str_t * s) {
	if (s == NULL || ptr == NULL || size == 0 || nmemb == 0) {
		return 0;
	}
	s->len = (nmemb + s->len);
	if ((s->ptr = (char *)realloc(s->ptr, nmemb + s->len)) == NULL) {
		s->len = 0;
		return 0;
	}
	strncat(s->ptr, ptr, nmemb);
	return size * nmemb;
}

static int
transport_call(transport_head_t * head, const char * url, int trans_method, const char * payload) {
	char request_url[TRANSPORT_CALL_URL_LEN];
	struct curl_slist *headers = NULL;
	CURLcode res;
	int ret = 0;

	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "charsets: utf-8");
	curl_easy_setopt(head->curl, CURLOPT_HTTPHEADER, headers);

	snprintf(request_url, TRANSPORT_CALL_URL_LEN, "http://%s/%s", head->host, url);

	curl_easy_setopt(head->curl, CURLOPT_URL, request_url);

	curl_easy_setopt(head->curl, CURLOPT_PORT, head->port);
	curl_easy_setopt(head->curl, CURLOPT_TIMEOUT, head->timeout);

	switch (trans_method) {
	case TRANS_METHOD_GET:
		curl_easy_setopt(head->curl, CURLOPT_CUSTOMREQUEST, "GET");
		/**
		 * Force the HTTP request to get back to using GET if a POST, HEAD, PUT, etc has
		 * been used previously using the same curl handle.
		 */
		curl_easy_setopt(head->curl, CURLOPT_HTTPGET, 1L);
		break;
	case TRANS_METHOD_POST:
		curl_easy_setopt(head->curl, CURLOPT_CUSTOMREQUEST, "POST");
		if (payload != NULL) {
			curl_easy_setopt(head->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	case TRANS_METHOD_PUT:
		curl_easy_setopt(head->curl, CURLOPT_CUSTOMREQUEST, "PUT");
		if (payload != NULL) {
			curl_easy_setopt(head->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	case TRANS_METHOD_DELETE:
		curl_easy_setopt(head->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		if (payload != NULL) {
			curl_easy_setopt(head->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	}

	curl_easy_setopt(head->curl, CURLOPT_FORBID_REUSE, 0);

	curl_easy_setopt(head->curl, CURLOPT_WRITEFUNCTION, memorize_response);
	curl_easy_setopt(head->curl, CURLOPT_WRITEDATA, &head->response);

	if ((res = curl_easy_perform(head->curl)) != CURLE_OK) {
		ret = res; // return curl error code
	}

	return ret;
}

static transport_head_t *
transport_create(const char *host, short port, int timeout) {
	transport_head_t * head = NULL;

	if ((head = malloc(sizeof (transport_head_t))) == NULL) {
		return NULL;
	}

	if ((head->curl = curl_easy_init()) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize curl!\n");
		return NULL;
	}

	if ((head->response.ptr = malloc(1)) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize response ptr!\n");
		return NULL;
	}
	head->response.len = 0;

	strncpy(head->host, host, TRANSPORT_HOST_LEN);
	head->port = port;
	head->timeout = timeout;

	return head;
}

static int
transport_get(transport_head_t * head, const char * path) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_GET, NULL);
}

static int
transport_post(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_POST, payload);
}

static int
transport_put(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_PUT, payload);
}

static int
transport_delete(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_DELETE, payload);
}

static void
transport_destroy(transport_head_t * head) {
	if (head == NULL) {
		return;
	}
	if (head->response.ptr != NULL) {
		free(head->response.ptr);
		head->response.ptr = NULL;
	}
	head->response.len = 0;
	curl_easy_cleanup(head->curl);
}

static const char *
transport_strerror(int error) {
	// handle curl errors
	if (error < 101) {
		return curl_easy_strerror(error);
	// handle internal errors
	} else {
		switch (error) {
		case TRANS_ERROR_INPUT:
			return "Input error";
		default:
			return "Unknown error";
		}
	}
}

_transport_t const transport = {
	transport_create,
	transport_get,
	transport_post,
	transport_put,
	transport_delete,
	transport_strerror,
	transport_destroy
};
