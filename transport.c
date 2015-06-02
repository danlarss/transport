#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libconfig.h>
#include <time.h>
#include "transport.h"

static inline int transport_build_url(const char *, const char *, const char *, char *, size_t);
static size_t transport_memorize_response(void *, size_t, size_t, void *);
static int transport_call(transport_session_t *, const char *, int, const char *);
static transport_session_t * transport_create(const char *);
static int transport_http_get(transport_session_t *, const char *);
static int transport_http_post(transport_session_t *, const char *, const char *);
static int transport_http_put(transport_session_t *, const char *, const char *);
static int transport_http_delete(transport_session_t *, const char *, const char *);
static const char * transport_strerror(int);
static int transport_search(transport_session_t *, const char *, const char *, const char *);
static int transport_create_index(transport_session_t *, const char *, const char *);
static int transport_delete_index(transport_session_t *, const char *);
static int transport_index_document(transport_session_t *, const char *, const char *, const char *, const char *);
static int transport_refresh(transport_session_t *, const char *);
static void transport_destroy(transport_session_t *);
static void transport_session_id(char *, size_t);

/**
 * @brief Function to generate a string of random chars.
 *
 * @param str pointer to string
 * @param size size of string
 */
static void 
transport_session_id(char * str, size_t size) {
    const char charset[] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVXYZ";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
}

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
	transport_session_t * session = (transport_session_t *) userp;

	/* overwrite previous result if flush_response is set to 1 */
	if (session->flush_response == 1) {
		session->response.pos = 0;
	}

	/* check to see if this data exceeds the size of our buffer. If so, 
	 * return 0 to indicate a problem to curl. */
	if (session->response.pos + realsize > TRANSPORT_MAX_RESPONSE_BUFFER) {
		return 0;
	}
		
	/* copy the data from the curl buffer into our response buffer */
	memcpy((void *)&session->response.buffer[session->response.pos], ptr, realsize);

	/* update the response string pos */
	session->response.pos += realsize;

	/* the data must be zero terminated */
	session->response.buffer[session->response.pos] = 0;
	
	return realsize;
}

/**
 * @brief Performs a http request using curl.
 *
 * @param session transport session struct
 * @param path URL path
 * @param trans_method HTTP request method (enum)
 * @param payload HTTP request body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_call(transport_session_t * session, const char * path, int trans_method, const char * payload) {
	char request_url[TRANSPORT_CALL_URL_LEN];
	struct curl_slist *headers = NULL;
	CURLcode res;
	int ret = 0;

	if (session == NULL) {
		return TRANS_ERROR_INPUT;
	}

	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "charsets: utf-8");
	curl_easy_setopt(session->curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(session->curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	curl_easy_setopt(session->curl, CURLOPT_TIMEOUT, session->timeout);

	switch (trans_method) {
	case TRANS_METHOD_GET:
		curl_easy_setopt(session->curl, CURLOPT_CUSTOMREQUEST, "GET");
		/**
		 * Force the HTTP request to get back to using GET if a POST, HEAD, PUT, etc has
		 * been used previously using the same curl handle.
		 */
		curl_easy_setopt(session->curl, CURLOPT_HTTPGET, 1L);
		break;
	case TRANS_METHOD_POST:
		curl_easy_setopt(session->curl, CURLOPT_CUSTOMREQUEST, "POST");
		if (payload != NULL) {
			curl_easy_setopt(session->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	case TRANS_METHOD_PUT:
		curl_easy_setopt(session->curl, CURLOPT_CUSTOMREQUEST, "PUT");
		if (payload != NULL) {
			curl_easy_setopt(session->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	case TRANS_METHOD_DELETE:
		curl_easy_setopt(session->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		if (payload != NULL) {
			curl_easy_setopt(session->curl, CURLOPT_POSTFIELDS, payload);
		}
		break;
	}

	curl_easy_setopt(session->curl, CURLOPT_FORBID_REUSE, 0);
	curl_easy_setopt(session->curl, CURLOPT_WRITEFUNCTION, transport_memorize_response);
	curl_easy_setopt(session->curl, CURLOPT_WRITEDATA, session);

	for (int i = 0; i < session->num_hosts; i++) {
		snprintf(request_url, TRANSPORT_CALL_URL_LEN, "%s/%s", session->hosts[i].host, path);
		curl_easy_setopt(session->curl, CURLOPT_PORT, session->hosts[i].port);
		curl_easy_setopt(session->curl, CURLOPT_URL, request_url);
		if ((res = curl_easy_perform(session->curl)) == CURLE_OK) {
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
 * session->reponse.buffer and the size in session->response.pos.
 *
 * @param session transport session struct.
 * @param index elastic index
 * @param type elastic type
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_search(transport_session_t * session, const char * index, const char * type, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, type, "_search", path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_post(session, path, payload);
}

/**
 * @brief Creates a new elastic index.
 *
 * @param session transport session struct.
 * @param index elastic index
 * @param payload HTTP PUT body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_create_index(transport_session_t * session, const char * index, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_put(session, path, payload);
}

/**
 * @brief Deletes an elastic index.
 *
 * @param session transport session struct.
 * @param index elastic index
 *
 * @return 0 on success or transport error code.
 */
static int
transport_delete_index(transport_session_t * session, const char * index) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_delete(session, path, NULL);
}

/**
 * @brief Stores a new document in elastic.
 *
 * @param session transport session struct.
 * @param index elastic index
 * @param type elastic type
 * @param id document id
 * @param payload HTTP PUT body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_index_document(transport_session_t * session, const char * index, const char * type, const char * id, const char * payload) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, type, id, path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_put(session, path, payload);
}

/**
 * @brief Explicitly refreshes an elastic index.
 *
 * @param session transport session struct.
 * @param index elastic index
 *
 * @return 0 on success or transport error code.
 */
static int
transport_refresh(transport_session_t * session, const char * index) {
	char path[TRANSPORT_CALL_URL_LEN];
	if (!transport_build_url(index, NULL, "_refresh", path, TRANSPORT_CALL_URL_LEN)) {
		return TRANS_ERROR_URL;
	}
	return transport_http_post(session, path, NULL);
}

/**
 * @brief Create and initialize a transport session struct.
 *
 * @param config Path to configuration file.
 *
 * @return a transport session struct.
 */
static transport_session_t *
transport_create(const char * config) {

	transport_session_t * session = NULL;
	config_t cfg;
	config_setting_t * setting;
	int host_count;

	config_init(&cfg);

	/* seeds the random number generator */
	srand((unsigned int)time(NULL) * getpid());

	/* allocate memory for session struct. */
	if ((session = malloc(sizeof (transport_session_t))) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize transport session.\n");
		return NULL;
	}

	/* initialize curl. */
	if ((session->curl = curl_easy_init()) == NULL) {
		fprintf(stderr, "transport.create() failed: could not initialize curl.\n");
		goto transport_create_error;
	}

	session->response.buffer[0] = '\0';
	session->response.pos = 0;

	/* generate a kind of unique session id */
	transport_session_id((char *)&session->id, TRANSPORT_SESSION_ID_LEN); 

	/* load config. */
	if (!config_read_file(&cfg, config)) {
		fprintf(stderr, "transport.create() failed: could not parse config file.");
		goto transport_create_error;
	}

	/* lookup timeout from config and store the value in session.  */
	if (!config_lookup_int(&cfg, "timeout", &session->timeout)) {
		session->timeout = TRANSPORT_DEFAULT_TIMEOUT;
	}

	/* lookup flush_response from config and store the value in session.  */
	if (!config_lookup_bool(&cfg, "flush_response", &session->flush_response)) {		
		session->flush_response = TRANSPORT_DEFAULT_FLUSH_REAPONSE;
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

	/* store hosts in session struct. */
	for (int i = 0; i < host_count && i < TRANSPORT_MAX_HOSTS; ++i) {
		const char * h = NULL;
		config_setting_t * host = config_setting_get_elem(setting, i);
		if (!(config_setting_lookup_string(host, "host", &h) && config_setting_lookup_int(host, "port", &session->hosts[session->num_hosts].port))) {
			continue;
		}
		strncpy(session->hosts[session->num_hosts].host, h, TRANSPORT_HOST_LEN);
		session->num_hosts++;
	}

	config_destroy(&cfg);
	return session;

transport_create_error:
	/* cleanup */
	if (session != NULL) {	
		if (session->curl != NULL) {
			curl_easy_cleanup(session->curl);
		}
		free(session);
		session = NULL;
	}
	config_destroy(&cfg);
	return NULL;
}

/**
 * @brief Perform a HTTP GET request.
 *
 * @param session transport session struct.
 * @param path URL path
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_get(transport_session_t * session, const char * path) {
	if (session == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(session, path, TRANS_METHOD_GET, NULL);
}

/**
 * @brief Perform a HTTP POST request.
 *
 * @param session transport session struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_post(transport_session_t * session, const char * path, const char * payload) {
	if (session == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(session, path, TRANS_METHOD_POST, payload);
}

/**
 * @brief Perform a HTTP PUT request.
 *
 * @param session transport session struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_put(transport_session_t * session, const char * path, const char * payload) {
	if (session == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(session, path, TRANS_METHOD_PUT, payload);
}

/**
 * @brief Perform a HTTP DELETE request.
 *
 * @param session transport session struct.
 * @param path URL path
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_http_delete(transport_session_t * session, const char * path, const char * payload) {
	if (session == NULL) {
		return TRANS_ERROR_INPUT;
	}
	return transport_call(session, path, TRANS_METHOD_DELETE, payload);
}

/**
 * @brief Cleanup transport session struct.
 *
 * @param session transport session struct.
 */
static void
transport_destroy(transport_session_t * session) {
	if (session == NULL) {
		return;
	}
	session->response.pos = 0;
	if (session->curl != NULL) {
		curl_easy_cleanup(session->curl);
	}
	free(session);
	session = NULL;
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
	transport_http_get,
	transport_http_post,
	transport_http_put,
	transport_http_delete,
	transport_refresh,
	transport_strerror,
	transport_destroy
};

int main(int argc, char **argv) {
	fprintf(stdout,"%s Version %d.%d\n", argv[0], TRANSPORT_VERSION_MAJOR, TRANSPORT_VERSION_MINOR);
	return 0;
}