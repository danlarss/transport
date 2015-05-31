#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libconfig.h>
#include "transport.h"

static inline int transport_build_url(const char *, const char *, const char *, char *, size_t);
static size_t transport_memorize_response(void *, size_t, size_t, void *);
static int transport_call(transport_head_t *, const char *, int, const char *);
static int transport_search(transport_head_t *, const char *, const char *, const char *);
static transport_head_t * transport_create(const char *);
static int transport_http_get(transport_head_t *, const char *);
static int transport_http_post(transport_head_t *, const char *, const char *);
static int transport_http_put(transport_head_t *, const char *, const char *);
static int transport_http_delete(transport_head_t *, const char *, const char *);
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
			written = snprintf(path, path_len, "%s", index);	
		} else {
			written = snprintf(path, path_len, "%s/%s", index, action);
		}
	} else {
		if (action == NULL || strlen(action) == 0) {
			written = snprintf(path, path_len, "%s/%s", index, type);
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
	transport_head_t * head = (transport_head_t *) userp;

	/* overwrite previous result if flush_response is set to 1 */
	if (head->flush_response == 1) {
		head->response.pos = 0;
	}

	/* check to see if this data exceeds the size of our buffer. If so, 
	 * return 0 to indicate a problem to curl. */
	if (head->response.pos + realsize > TRANSPORT_MAX_RESPONSE_BUFFER) {
		return 0;
	}
		
	/* copy the data from the curl buffer into our response buffer */
	memcpy((void *)&head->response.buffer[head->response.pos], ptr, realsize);

	/* update the response string pos */
	head->response.pos += realsize;

	/* the data must be zero terminated */
	head->response.buffer[head->response.pos] = 0;
	
	return realsize;
}

/**
 * @brief Performs a http request using curl.
 *
 * @param head transport head struct
 * @param path URL path
 * @param trans_method HTTP request method (enum)
 * @param payload HTTP request body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_call(transport_head_t * head, const char * path, int trans_method, const char * payload) {
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
	curl_easy_setopt(head->curl, CURLOPT_WRITEDATA, head);

	for (int i = 0; i < head->num_hosts; i++) {
		snprintf(request_url, TRANSPORT_CALL_URL_LEN, "%s/%s", head->hosts[i].host, path);
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

/**
 * @brief Performs an elastic search.
 *
 * On a successfull search a pointer to the respose is stored in
 * head->reponse.buffer and the size in head->response.pos.
 *
 * @param head transport head struct.
 * @param index elastic index
 * @param type elastic type
 * @param payload HTTP post body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_search(transport_head_t * head, const char * index, const char * type, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, type, "_search", path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_post(head, path, payload);
}

static int
transport_create_index(transport_head_t * head, const char * index, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_put(head, path, payload);
}

static int
transport_delete_index(transport_head_t * head, const char * index) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_delete(head, path, NULL);
}

static int
transport_index_document(transport_head_t * head, const char * index, const char * type, const char * id, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, type, id, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_put(head, path, payload);
}

/**
 * @brief Create and initialize a transport head struct.
 *
 * @param config Path to configuration file.
 *
 * @return a transport head struct.
 */
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
		goto transport_create_error;
	}

	head->response.buffer[0] = '\0';
	head->response.pos = 0;

	/* load config. */
	if (!config_read_file(&cfg, config)) {
		fprintf(stderr, "transport.create() failed: could not parse config file.");
		goto transport_create_error;
	}

	/* lookup timeout from config and store the value in head.  */
	if (!config_lookup_int(&cfg, "timeout", &head->timeout)) {
		head->timeout = TRANSPORT_DEFAULT_TIMEOUT;
	}

	/* lookup flush_response from config and store the value in head.  */
	if (!config_lookup_bool(&cfg, "flush_response", &head->flush_response)) {		
		head->flush_response = TRANSPORT_DEFAULT_FLUSH_REAPONSE;
	}

	/* load hosts from config. */
	if ((setting = config_lookup(&cfg, "hosts")) == NULL) {
		goto transport_create_error;
	}
	host_count = config_setting_length(setting);
	if (host_count == 0) {
		fprintf(stderr, "transport.create() failed: missing 'hosts' in configuration file.\n");
		goto transport_create_error;
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

transport_create_error:
	/* cleanup */
	if (head != NULL) {	
		if (head->curl != NULL) {
			curl_easy_cleanup(head->curl);
		}
		free(head);
		head = NULL;
	}
	config_destroy(&cfg);
	return NULL;
}

/**
 * @brief Perform a HTTP GET request.
 *
 * @param head transport head struct.
 * @param path URL path
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_get(transport_head_t * head, const char * path) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_GET, NULL);
}

/**
 * @brief Perform a HTTP POST request.
 *
 * @param head transport head struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_post(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_POST, payload);
}

/**
 * @brief Perform a HTTP PUT request.
 *
 * @param head transport head struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_put(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_PUT, payload);
}

/**
 * @brief Perform a HTTP DELETE request.
 *
 * @param head transport head struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_delete(transport_head_t * head, const char * path, const char * payload) {
	if (head == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(head, path, TRANS_METHOD_DELETE, payload);
}

/**
 * @brief Cleanup transport head struct.
 *
 * @param head transport head struct.
 */
static void
transport_destroy(transport_head_t * head) {
	if (head == NULL) {
		return;
	}
	head->response.pos = 0;
	if (head->curl != NULL) {
		curl_easy_cleanup(head->curl);
	}
	free(head);
	head = NULL;
}

/**
 * @brief Returns a pointer to a string that describes the error 
 * code passed in the argument error.
 *
 * @param error transport error code.
 *
 * @return pointer to error description string.
 */
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
	transport_create_index,
	transport_delete_index,
	transport_index_document,
	transport_strerror,
	transport_destroy
};