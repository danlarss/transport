#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libconfig.h>
#include "transport.h"

static inline int transport_build_url(const char *, const char *, const char *, char *, size_t);
static size_t transport_memorize_response(void *, size_t, size_t, void *);
static int transport_call(transport_head_t *, const char *, int, const char *);
static int transport_search(transport_head_t *, const char *, const char *, const char *);
static transport_head_t * transport_create(const char *);
static int transport_get(transport_head_t *, const char *);
static int transport_post(transport_head_t *, const char *, const char *);
static int transport_put(transport_head_t *, const char *, const char *);
static int transport_delete(transport_head_t *, const char *, const char *);
static void transport_destroy(transport_head_t *);
static const char * transport_strerror(int);

/**
 * @brief Function to construct URL to the elastic host.
 *
 * @param index		The elastic index.
 * @param type 		The elastic type
 * @param action 	The elastic page
 * @param path_len 	Size of the path buffer
 * 
 * @return The number of bytes written if the buffer was large enough, 
 * 0 on failure.
 */
static inline int
transport_build_url(const char * index, const char * type, const char * action, char * path, size_t path_len) {
	int written = 0;
	if (index == NULL || strlen(index) == 0) {
		return 0;
	}
	if (type == NULL || strlen(type) == 0) {
		if (action == NULL || strlen(action) == 0) {
			written = snprintf(path, path_len, "%s/", index);	
		} else {
			written = snprintf(path, path_len, "%s/%s", index, action);
		}
	} else {
		if (action == NULL || strlen(action) == 0) {
			written = snprintf(path, path_len, "%s/%s/", index, type);
		} else {
			written = snprintf(path, path_len, "%s/%s/%s", index, type, action);
		}
	}
	return written;
}

/**
 * @brief curl write data callback function called within the context of 
 * curl_easy_perform.
 *
 * @param ptr pointer to the delivered data.
 * @param size size of 1 piece of data
 * @param nmemb number of pieces
 * @param userp user data
 *
 * @return the number of bytes actually taken care of.
 */
static size_t
transport_memorize_response(void *ptr, size_t size, size_t nmemb, void * userp) {

	if (userp == NULL || ptr == NULL || size == 0 || nmemb == 0) {
		return 0;
	}

	size_t realsize = size * nmemb;
  	str_t * mem = (str_t *) userp;

	if ((nmemb + mem->size) > TRANSPORT_MAX_RESPONSE_BUFFER) {
		/* response to large */
		return 0;
	}
 
 	mem->ptr = realloc(mem->ptr, mem->size + realsize + 1);
	if (mem->ptr == NULL) {
		/* out of memory! */ 
		return 0;
	}
 
 	memcpy(&(mem->ptr[mem->size]), ptr, realsize);
 	mem->size += realsize;
 	/* the data must be zero terminated */
 	mem->ptr[mem->size] = 0;
 
 	return realsize;
}

static int
transport_call(transport_head_t * head, const char * url, int trans_method, const char * payload) {
	char request_url[TRANSPORT_CALL_URL_LEN];
	struct curl_slist *headers = NULL;
	CURLcode res;
	int ret = 0;

	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}

	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "charsets: utf-8");
	curl_easy_setopt(head->curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(head->curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
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
	curl_easy_setopt(head->curl, CURLOPT_WRITEFUNCTION, transport_memorize_response);
	curl_easy_setopt(head->curl, CURLOPT_WRITEDATA, &head->response);

	for (int i = 0; i < head->num_hosts; i++) {
		snprintf(request_url, TRANSPORT_CALL_URL_LEN, "%s/%s", head->hosts[i].host, url);
		fprintf(stderr, "url: %s\n", request_url);

		curl_easy_setopt(head->curl, CURLOPT_PORT, head->hosts[i].port);
		curl_easy_setopt(head->curl, CURLOPT_URL, request_url);
		if ((res = curl_easy_perform(head->curl)) == CURLE_OK) {
			ret = 0;
			break;
		} else {
			ret = res; /* return curl error code */
		}
	}

	return ret;
}


static int
transport_search(transport_head_t * head, const char * index, const char * type, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, type, "_search", path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_post(head, path, payload);
}

static transport_head_t *
transport_create(const char * config) {

	transport_head_t * head = NULL;
	config_t cfg;
	config_setting_t * setting;
	int host_count;

	config_init(&cfg);

	/* allocate memory for head struct. */
	if ((head = malloc(sizeof (transport_head_t))) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize transport head.\n");
		return NULL;
	}

	/* initialize curl. */
	if ((head->curl = curl_easy_init()) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize curl.\n");
		return NULL;
	}

	/* allocate initial memory for the response pointer (will be resized). */
	if ((head->response.ptr = malloc(16)) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize response ptr.\n");
		curl_easy_cleanup(head->curl);
		config_destroy(&cfg);
		return NULL;
	}
	head->response.size = 0;

	/* load config. */
	if (!config_read_file(&cfg, config)) {
		fprintf(stderr, "transport.create() failed: %s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
		config_destroy(&cfg);
    	return NULL;
    }

	/* lookup timeout from config and store the value in head.  */
	if (!config_lookup_int(&cfg, "timeout", &head->timeout)) {
    	fprintf(stderr, "transport.create() failed: missing 'timeout' in configuration file.\n");
		config_destroy(&cfg);
    	return NULL;
    }

	/* load hosts from config. */
	if ((setting = config_lookup(&cfg, "hosts")) == NULL) {
		config_destroy(&cfg);
		return NULL;
	}
	host_count = config_setting_length(setting);
	if (host_count == 0) {
    	fprintf(stderr, "transport.create() failed: missing 'hosts' in configuration file.\n");
		config_destroy(&cfg);
    	return NULL;		
	}

	/* store hosts in head struct. */
	for (int i = 0; i < host_count && i < TRANSPORT_MAX_HOSTS; ++i) {
		const char * h = NULL;
    	config_setting_t * host = config_setting_get_elem(setting, i);
		if (!(config_setting_lookup_string(host, "host", &h) && config_setting_lookup_int(host, "port", &head->hosts[head->num_hosts].port))) {
			continue;
		}
		strncpy(head->hosts[head->num_hosts].host, h, TRANSPORT_HOST_LEN);
		head->num_hosts++;
	}
	config_destroy(&cfg);
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
	head->response.size = 0;
	curl_easy_cleanup(head->curl);
}

static const char *
transport_strerror(int error) {
	/* handle curl errors 0 - 89 */
	if (error < 90) {
		return curl_easy_strerror(error);
	/* handle internal errors */
	} else {
		switch (error) {
		case TRANS_ERROR_INPUT:
			return "Input error";
		case TRANS_ERROR_URL:
			return "URL error";
		default:
			return "Unknown error";
		}
	}
}

_transport_t const transport = {
	transport_create,
	transport_search,
	transport_strerror,
	transport_destroy
};

/* TEST
int main(int argc, char **argv) {
	fprintf(stdout, "Start\n");
	int ret;

	transport_head_t * head = transport_create("transport.cfg");
	ret = transport.search(head, "foods", "food", "{\"query\":{\"filtered\":{\"query\":{\"match\":{\"name\":\"smör\"}}}}}");
	if (ret != 0) {
		fprintf(stderr, "transport.search() failed: %s\n", transport_strerror(ret));
	} else {
		fprintf(stdout, "Response: %s\n", head->response.ptr);
	}
	transport.destroy(head);
	fprintf(stdout, "Done\n");
	return 0;
}
*/