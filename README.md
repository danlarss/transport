# Transport

Transport is a simple helper library for communicating with elastic search.

## Synopsis

```c
#include <transport.h>

transport_session_t * transport.create(const char *);
int transport.http_get(transport_session_t *, const char *);
int transport.http_post(transport_session_t *, const char *, const char *);
int transport.http_put(transport_session_t *, const char *, const char *);
int transport.http_delete(transport_session_t *, const char *, const char *);
int transport.search(transport_session_t *, const char *, const char *, const char *);
int transport.create_index(transport_session_t *, const char *, const char *);
int transport.delete_index(transport_session_t *, const char *);
int transport.index_document(transport_session_t *, const char *, const char *, const char *, const char *);
int transport.refresh(transport_session_t *, const char *);
const char * transport.strerror(int);
void transport_destroy(transport_session_t *);
```

## Install

In build dir
```
$ make && make install
```

## Usage:
*settings.cfg*
```
hosts = ({ host = "http://127.0.0.1";
           port = 9200; },
         { host = "http://127.0.0.1";
           port = 9210; }
         );
timeout = 1;
```

*test.c*
```c
#include <stdio.h>
#include <transport.h>

int main(int argc, char **argv) {
  
    int res;
    char * index = "myindex",
         * type = "mytype",
         * index_payload = "{\"settings\": {\"number_of_shards\":1,\"number_of_replicas\":0},\"mappings\": {\"mytype\": {\"properties\": {\"name\":{\"type\": \"string\"}}}}}",
         * query = "{\"query\":{\"filtered\":{\"query\":{\"match\":{\"name\":\"hello\"}}}}}";

    /* start a new transport session */
    transport_session_t * session = transport.create("settings.cfg");
    if (!session) {
        return 1;
    }

    /* create index "myindex" */
    if ((res = transport.create_index(session, index, index_payload)) != 0) {
        fprintf(stderr, "%s: \"%s\"\n", transport.strerror(res), TRANSPORT_GET_ERROR(session));
    }

    /* index two new documents */
    if ((res = transport.index_document(session, index, type, "id-1", "{\"name\":\"Hello world\"}")) != 0) {
        fprintf(stderr, "%s: \"%s\"\n", transport.strerror(res), TRANSPORT_GET_ERROR(session));
    }
    if ((res = transport.index_document(session, index, type, "id-2", "{\"name\":\"Hello nothing\"}")) != 0) {
        fprintf(stderr, "%s: \"%s\"\n", transport.strerror(res), TRANSPORT_GET_ERROR(session));
    }

    /* explicitly refresh the new index */
    if ((res = transport.refresh(session, index)) != 0) {
        fprintf(stderr, "%s: \"%s\"\n", transport.strerror(res), TRANSPORT_GET_ERROR(session));
    }

    /* search the index */
    if ((res = transport.search(session, index, type, query)) != 0) {
        fprintf(stderr, "%s: \"%s\"\n", transport.strerror(res), TRANSPORT_GET_ERROR(session));
    } else {
        /* print out the raw result */
        fprintf(stdout, "%s\n", session->raw.buffer);
       
        /* print out individual hits */
        fprintf(stdout, "Found: %d results\n", session->search.hits.total);
        for (int i = 0; i < session->search.hits.total; i++) {
            fprintf(stdout, "%s: %s\n", session->search.hits.hits[i]._id, session->search.hits.hits[i]._source);
        }
    }

    /* drop the new index */
    transport.delete_index(session, index);

    /* cleanup */
    transport.destroy(session);
    return 0;
}
```

## Functions

### transport.create

```c
transport_session_t * transport.create(const char * config);
```
Create and initialize a transport session struct.

**Parameters**
 - *config* Path to config file.

**Return**
 - A transport session struct.

### transport.destroy

```c
void transport.destroy(transport_session_t * session);
```
Free transport session struct.

**Parameters**
 - *session* Transport session struct.

### transport.search

```c
int transport.search(transport_session_t * session, const char * index, const char * type, const char * payload);
```
Perform an elastic search.

**Parameters**
 - *session* Transport session struct.
 - *index* Elastic index name
 - *type* Elastic document type name
 - *payload* HTTP POST body in JSON format

**Return**
 - 0 on success or a transport error code.

### transport.create_index

```c
int transport.create_index(transport_session_t * session, const char * index, const char * payload);
```
Create a new elastic index.

**Parameters**
 - *session* Transport session struct.
 - *index* Elastic index name
 - *payload* HTTP PUT body in JSON format

**Return**
 - 0 on success or a transport error code.

### transport.delete_index

```c
int transport.delete_index(transport_session_t * session, const char * index);
```
Delete an elastic index.

**Parameters**
 - *session* Transport session struct.
 - *index* Elastic index name
 
**Return**
 - 0 on success or a transport error code.

### transport.index_document

```c
int transport.index_document(transport_session_t * session, const char * index, const char * type, const char * id, const char * payload)
```
Index an elastic document.

**Parameters**
 - *session* Transport session struct.
 - *index* Elastic index name
 - *type* Elastic document type name
 - *id* Document ID
 - *payload* HTTP POST body in JSON format

**Return**
 - 0 on success or a transport error code.

### transport.strerror

```c
const char * transport.strerror(int error);
```
Returns a pointer to a string that describes the error code passed in the argument error.

**Parameters**
 - *error* Transport error code.

**Return**
 - Pointer to error description string

### transport.refresh

```c
int transport.refresh(transport_session_t * session, const char * index);
```
Explicitly refresh an index.

**Parameters**
 - *session* Transport session struct.
 - *index* Elastic index name

**Return**
 - 0 on success or a transport error code.

