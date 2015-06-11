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
static void transport_yajl_copy_callback(void *ctx, const char *str, size_t len);
static void transport_yajl_check_status(yajl_gen_status status);
static void transport_yajl_serialize_value(yajl_gen gen, yajl_val val);
static void transport_yajl_copy_tree(str_t *f, yajl_val tree);


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
 * @param index     The elastic index.
 * @param type      The elastic type
 * @param action    The elastic page
 * @param path_len  Size of the path buffer
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
    
    char * body;
    yajl_val node, v;

    if (userp == NULL || ptr == NULL || size == 0 || nmemb == 0) {
        return 0;
    }

    size_t realsize = size * nmemb;
    transport_session_t * session = (transport_session_t *) userp;

    /* reset response string */
    session->raw.pos = 0;

    /* check to see if this data exceeds the size of our buffer. If so, 
     * return 0 to indicate a problem to curl. */
    if (session->raw.pos + realsize > TRANSPORT_RESPONSE_LEN) {
        return 0;
    }

    /* copy the data from the curl buffer into our response buffer */
    memcpy((void *)&session->raw.buffer[session->raw.pos], ptr, realsize);

    /* update the response string pos */
    session->raw.pos += realsize;

    /* the data must be zero terminated */
    session->raw.buffer[session->raw.pos] = 0;

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
 * On a successfull search a pointer to the raw respose is stored in
 * session->raw.buffer and the size in session->raw.pos.
 *
 * @param session transport session struct.
 * @param index elastic index
 * @param type elastic typ
 * @param payload HTTP POST body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_search(transport_session_t * session, const char * index, const char * type, const char * payload) {
    char path[TRANSPORT_CALL_URL_LEN];
    const char * took_path[] = {"took", NULL},
               * timed_out_path[] = {"timed_out", NULL},
               * total_path[] = {"_shards", "total", NULL},
               * successful_path[] = {"_shards", "successful", NULL},
               * failed_path[] = {"_shards", "failed", NULL},
               * status_path[] = {"status", NULL},
               * error_path[] = {"error", NULL},
               * hits_total_path[] = {"hits", "total", NULL},
               * hits_max_score_path[] = {"hits", "max_score", NULL},
               * hits_hits_path[] = {"hits", "hits", NULL},
               * index_path[] = {"_index", NULL},
               * type_path[] = {"_type", NULL},
               * score_path[] = {"_score", NULL},
               * source_path[] = {"_source", NULL},
               * id_path[] = {"_id", NULL};

    yajl_val node, v, h;
    int ret = 0;
    char eb[1024];

    session->type = TRANS_SESSION_TYPE_NONE;

    if (!transport_build_url(index, type, "_search", path, TRANSPORT_CALL_URL_LEN)) {
        return TRANS_ERROR_URL;
    }
    ret = transport_http_post(session, path, payload);
    if (ret != 0) {
        return ret;
    }

    /* parse response */
    node = yajl_tree_parse(session->raw.buffer, eb, sizeof(eb));
    if (node == NULL) {
        return TRANS_ERROR_PARSE;
    }

    /* store error and status, if any, in document response */
    if ((v = yajl_tree_get(node, error_path, yajl_t_string)) != NULL) {
        ret = TRANS_ERROR_ELASTIC;
        strncpy(session->error.error, YAJL_GET_STRING(v), TRANSPORT_ERROR_LEN);
        if ((v = yajl_tree_get(node, status_path, yajl_t_number)) != NULL) {
            session->error.status = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_ERROR;
    } else {
        if ((v = yajl_tree_get(node, took_path, yajl_t_number)) != NULL) {
            session->search.took = YAJL_GET_INTEGER(v);
        }
        if ((v = yajl_tree_get(node, timed_out_path, yajl_t_true)) != NULL) {
            session->search.timed_out = YAJL_IS_TRUE(v) ? 1 : 0;
        }
        if ((v = yajl_tree_get(node, total_path, yajl_t_number)) != NULL) {
            session->search._shards.total = YAJL_GET_INTEGER(v);
        }
        if ((v = yajl_tree_get(node, successful_path, yajl_t_number)) != NULL) {
            session->search._shards.successful = YAJL_GET_INTEGER(v);
        }
        if ((v = yajl_tree_get(node, failed_path, yajl_t_number)) != NULL) {
            session->search._shards.failed = YAJL_GET_INTEGER(v);
        }
        if ((v = yajl_tree_get(node, hits_total_path, yajl_t_number)) != NULL) {
            session->search.hits.total = YAJL_GET_INTEGER(v);
        }
        if ((v = yajl_tree_get(node, hits_max_score_path, yajl_t_number)) != NULL) {
            session->search.hits.max_score = YAJL_GET_DOUBLE(v);
        }
        if ((v = yajl_tree_get(node, hits_hits_path, yajl_t_array)) != NULL) {
            size_t len = v->u.array.len;
            for (int i = 0; i < len && i < TRANSPORT_MAX_NUM_HITS; ++i) {
                yajl_val obj = v->u.array.values[i];
                if ((h = yajl_tree_get(obj, index_path, yajl_t_string)) != NULL) {
                    strncpy(session->search.hits.hits[i]._index, YAJL_GET_STRING(h), TRANSPORT_INDEX_LEN);
                }
                if ((h = yajl_tree_get(obj, type_path, yajl_t_string)) != NULL) {
                    strncpy(session->search.hits.hits[i]._type, YAJL_GET_STRING(h), TRANSPORT_TYPE_LEN);
                }
                if ((h = yajl_tree_get(obj, id_path, yajl_t_string)) != NULL) {
                    strncpy(session->search.hits.hits[i]._id, YAJL_GET_STRING(h), TRANSPORT_ID_LEN);
                }
                if ((h = yajl_tree_get(obj, score_path, yajl_t_number)) != NULL) {
                    session->search.hits.hits[i]._score = YAJL_GET_DOUBLE(h);
                }
                if ((h = yajl_tree_get(obj, source_path, yajl_t_object)) != NULL) {
                    if (YAJL_IS_OBJECT(h)) {
                        str_t str = {0};
                        transport_yajl_copy_tree(&str, h);
                        strncpy(session->search.hits.hits[i]._source, str.buffer, str.pos);
                    } else {
                        strncpy(session->search.hits.hits[i]._source, YAJL_GET_STRING(h), TRANSPORT_SOURCE_LEN);
                    }
                } else {
                    strncpy(session->search.hits.hits[i]._source, "Not an object!", 16);
                }
            }
        }
        session->type = TRANS_SESSION_TYPE_SEARCH;
    }
    return ret;
}


static void 
transport_yajl_copy_callback(void *ctx, const char *str, size_t len) {
    str_t * f = (str_t *) ctx;
    memcpy((void *)&f->buffer[f->pos], str, len);
    f->pos += len;
}
 
static void 
transport_yajl_check_status(yajl_gen_status status) {
    if (status != yajl_gen_status_ok) {
        fprintf (stderr, "yajl_gen_status was %d\n", (int) status);
        exit (EXIT_FAILURE);
    }
}
 
static void
transport_yajl_serialize_value(yajl_gen gen, yajl_val val) {
    size_t i;

    switch(val->type) {
    case yajl_t_string:
        transport_yajl_check_status(yajl_gen_string (gen, (const unsigned char *) val->u.string, strlen (val->u.string)));
        break;
    case yajl_t_number:
        transport_yajl_check_status(yajl_gen_number(gen, YAJL_GET_NUMBER (val), strlen (YAJL_GET_NUMBER (val))));
        break;
    case yajl_t_object:
        transport_yajl_check_status(yajl_gen_map_open(gen));
        for (i = 0 ;  i < val->u.object.len; i++) {
            transport_yajl_check_status(yajl_gen_string (gen, (const unsigned char *) val->u.object.keys[i], strlen (val->u.object.keys[i])));
            transport_yajl_serialize_value(gen, val->u.object.values[i]);
        }
        transport_yajl_check_status(yajl_gen_map_close(gen));
        break;
    case yajl_t_array:
        transport_yajl_check_status(yajl_gen_array_open(gen));
        for (i = 0; i < val->u.array.len;  i++) {
            transport_yajl_serialize_value(gen, val->u.array.values[i]);
        }
        transport_yajl_check_status(yajl_gen_array_close(gen));
        break;
    case yajl_t_true:
        transport_yajl_check_status(yajl_gen_bool(gen, 1));
        break;
    case yajl_t_false:
        transport_yajl_check_status(yajl_gen_bool(gen, 0));
        break;
    case yajl_t_null:
        transport_yajl_check_status(yajl_gen_null(gen));
        break;
    default:
        fprintf (stderr, "unexpectedly got type %d\n", (int) val->type);
        exit (EXIT_FAILURE);
    }
}

static void
transport_yajl_copy_tree(str_t * f, yajl_val tree) {
    yajl_gen gen;
 
    if ((gen = yajl_gen_alloc(NULL)) == NULL) {
        fprintf (stderr, "yajl_gen_alloc failed\n");
        exit(EXIT_FAILURE);
    }
 
    if (0 == yajl_gen_config(gen, yajl_gen_print_callback, transport_yajl_copy_callback, f)) {
        fprintf(stderr, "yajl_gen_config failed\n");
        exit(EXIT_FAILURE);
    }

    transport_yajl_serialize_value(gen, tree);
    yajl_gen_free(gen);
}

/**
 * @brief Creates a new elastic index.
 *
 * @param session transport session struct.
 * @param index elastic index
 * @aram payload HTTP PUT body
 *
 * @return 0 on success or transport error code.
 */
static int
transport_create_index(transport_session_t * session, const char * index, const char * payload) {
    char path[TRANSPORT_CALL_URL_LEN];
    const char * acknowledged_path[] = {"acknowledged", NULL},
           * status_path[] = {"status", NULL},
           * error_path[] = {"error", NULL};
    yajl_val node, v;
    int ret = 0;
    char eb[1024];

    session->type = TRANS_SESSION_TYPE_NONE;

    if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
        return TRANS_ERROR_URL;
    }
    ret = transport_http_put(session, path, payload);
    if (ret != 0) {
        return ret;
    }

    /* parse response */
    node = yajl_tree_parse(session->raw.buffer, eb, sizeof(eb));
    if (node == NULL) {
        return TRANS_ERROR_PARSE;
    }

    /* store error and status, if any, in document response */
    if ((v = yajl_tree_get(node, error_path, yajl_t_string)) != NULL) {
        ret = TRANS_ERROR_ELASTIC;
        strncpy(session->error.error, YAJL_GET_STRING(v), TRANSPORT_ERROR_LEN);
        if ((v = yajl_tree_get(node, status_path, yajl_t_number)) != NULL) {
            session->error.status = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_ERROR;
    } else {
        if ((v = yajl_tree_get(node, acknowledged_path, yajl_t_true)) != NULL) {
            session->create_index.acknowledged = YAJL_IS_TRUE(v) ? 1 : 0;
        }
        session->type = TRANS_SESSION_TYPE_CREATE_INDEX;
    }

    return ret;
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
    const char * acknowledged_path[] = {"acknowledged", NULL},
           * status_path[] = {"status", NULL},
           * error_path[] = {"error", NULL};
    yajl_val node, v;
    int ret = 0;
    char eb[1024];

    session->type = TRANS_SESSION_TYPE_NONE;

    if (!transport_build_url(index, NULL, NULL, path, TRANSPORT_CALL_URL_LEN)) {
        return TRANS_ERROR_URL;
    }
    ret = transport_http_delete(session, path, NULL);
    if (ret != 0) {
        return ret;
    }
    
    /* parse response */
    node = yajl_tree_parse(session->raw.buffer, eb, sizeof(eb));
    if (node == NULL) {
        return TRANS_ERROR_PARSE;
    }

    /* store error and status, if any, in document response */
    if ((v = yajl_tree_get(node, error_path, yajl_t_string)) != NULL) {
        ret = TRANS_ERROR_ELASTIC;
        strncpy(session->error.error, YAJL_GET_STRING(v), TRANSPORT_ERROR_LEN);
        if ((v = yajl_tree_get(node, status_path, yajl_t_number)) != NULL) {
            session->error.status = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_ERROR;
    } else {
        if ((v = yajl_tree_get(node, acknowledged_path, yajl_t_true)) != NULL) {
            session->delete_index.acknowledged = YAJL_IS_TRUE(v) ? 1 : 0;
        }
        session->type = TRANS_SESSION_TYPE_DELETE_INDEX;
    }

    return ret;
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
    const char * index_path[] = {"_index", NULL},
               * type_path[] = {"_type", NULL},
               * id_path[] = {"_id", NULL},
               * version_path[] = {"_version", NULL},
               * created_path[] = {"created", NULL},
               * status_path[] = {"status", NULL},
               * error_path[] = {"error", NULL};
    yajl_val node, v;
    int ret = 0;
    char eb[1024];

    session->type = TRANS_SESSION_TYPE_NONE;

    if (!transport_build_url(index, type, id, path, TRANSPORT_CALL_URL_LEN)) {
        return TRANS_ERROR_URL;
    }
    ret = transport_http_put(session, path, payload);
    if (ret != 0) {
        return ret;
    }

    /* parse response */
    node = yajl_tree_parse(session->raw.buffer, eb, sizeof(eb));
    if (node == NULL) {
        return TRANS_ERROR_PARSE;
    }

    /* store error and status, if any, in document response */
    if ((v = yajl_tree_get(node, error_path, yajl_t_string)) != NULL) {
        ret = TRANS_ERROR_ELASTIC;
        strncpy(session->error.error, YAJL_GET_STRING(v), TRANSPORT_ERROR_LEN);
        if ((v = yajl_tree_get(node, status_path, yajl_t_number)) != NULL) {
            session->error.status = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_ERROR;
    } else {
        /* store index in document response */
        if ((v = yajl_tree_get(node, index_path, yajl_t_string)) != NULL) {
            strncpy(session->index_document._index, YAJL_GET_STRING(v), TRANSPORT_INDEX_LEN);
        }
        /* store type in document response */
        if ((v = yajl_tree_get(node, type_path, yajl_t_string)) != NULL) {
            strncpy(session->index_document._type, YAJL_GET_STRING(v), TRANSPORT_TYPE_LEN);
        }
        /* store id in document response */
        if ((v = yajl_tree_get(node, id_path, yajl_t_string)) != NULL) {
            strncpy(session->index_document._id, YAJL_GET_STRING(v), TRANSPORT_ID_LEN);
        }
        /* store version in document response */
        if ((v = yajl_tree_get(node, version_path, yajl_t_number)) != NULL) {
            session->index_document._version = atoi(YAJL_GET_NUMBER(v));
        }
        /* store created in document response */
        if ((v = yajl_tree_get(node, created_path, yajl_t_true)) != NULL) {
            session->index_document.created = YAJL_IS_TRUE(v) ? 1 : 0;
        }
        session->type = TRANS_SESSION_TYPE_INDEX_DOCUMENT;
    }

    return ret;
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
    const char * total_path[] = {"_shards", "total", NULL},
               * successful_path[] = {"_shards", "successful", NULL},
               * failed_path[] = {"_shards", "failed", NULL},
               * status_path[] = {"status", NULL},
               * error_path[] = {"error", NULL};
    yajl_val node, v;
    int ret = 0;
    char eb[1024];

    session->type = TRANS_SESSION_TYPE_NONE;

    if (!transport_build_url(index, NULL, "_refresh", path, TRANSPORT_CALL_URL_LEN)) {
        return TRANS_ERROR_URL;
    }
    ret = transport_http_post(session, path, NULL);
    if (ret != 0) {
        return ret;
    }

    /* parse response */
    node = yajl_tree_parse(session->raw.buffer, eb, sizeof(eb));
    if (node == NULL) {
        return TRANS_ERROR_PARSE;
    }

    /* store error and status, if any, in document response */
    if ((v = yajl_tree_get(node, error_path, yajl_t_string)) != NULL) {
        ret = TRANS_ERROR_ELASTIC;
        strncpy(session->error.error, YAJL_GET_STRING(v), TRANSPORT_ERROR_LEN);
        if ((v = yajl_tree_get(node, status_path, yajl_t_number)) != NULL) {
            session->error.status = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_ERROR;
    } else {
        if ((v = yajl_tree_get(node, total_path, yajl_t_number)) != NULL) {
            session->refresh._shards.total = atoi(YAJL_GET_NUMBER(v));
        }
        if ((v = yajl_tree_get(node, successful_path, yajl_t_number)) != NULL) {
            session->refresh._shards.successful = atoi(YAJL_GET_NUMBER(v));
        }
        if ((v = yajl_tree_get(node, failed_path, yajl_t_number)) != NULL) {
            session->refresh._shards.failed = atoi(YAJL_GET_NUMBER(v));
        }
        session->type = TRANS_SESSION_TYPE_REFRESH;
    }

    return ret;
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

    /* seed the random number generator */
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

    /* reset response buffer */
    session->raw.buffer[0] = '\0';
    session->raw.pos = 0;

    /* generate a kind of unique session id */
    transport_session_id((char *)&session->id, TRANSPORT_SESSION_ID_LEN); 

    /* load config. */
    if (!config_read_file(&cfg, config)) {
        fprintf(stderr, "transport.create() failed: could not parse config file.\n");
        goto transport_create_error;
    }

    /* lookup timeout from config and store the value in session.  */
    if (!config_lookup_int(&cfg, "timeout", &session->timeout)) {
        session->timeout = TRANSPORT_DEFAULT_TIMEOUT;
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
    if (&cfg != NULL) {
        config_destroy(&cfg);
    }
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
    session->raw.pos = 0;
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
    transport_refresh,
    transport_http_get,
    transport_http_post,
    transport_http_put,
    transport_http_delete,
    transport_strerror,
    transport_destroy
};

int main(int argc, char **argv) {
    fprintf(stdout,"%s Version %d.%d\n", argv[0], TRANSPORT_VERSION_MAJOR, TRANSPORT_VERSION_MINOR);
    return 0;
}