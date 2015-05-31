# transport

Transport is a simple helper library for communicating with elastic.

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

## Example usage:
*settings.cfg*
```
hosts = ({ host = "http://127.0.0.1";
           port = 9200; },
         { host = "http://127.0.0.1";
           port = 9210; }
         );
timeout = 1;
flush_response = true;
```

*test.c*
```c
#include <stdio.h>
#include "transport/transport.h"

int main(int argc, char **argv) {
  
    char * index = "myindex",
         * type = "mytype",
         * index_payload = "{\"settings\": {\"number_of_shards\":1,\"number_of_replicas\":0},\"mappings\": {\"mytype\": {\"properties\": {\"name\":{\"type\": \"string\"}}}}}",
         * query = "{\"query\":{\"filtered\":{\"query\":{\"match\":{\"name\":\"hello\"}}}}}";

    /* start a new transport session */
    transport_session_t * session = transport.create("transport.cfg");

    /* create index "myindex" */
    transport.create_index(session, index, index_payload);

    /* index two new documents */
    transport.index_document(session, index, type, "id-1", "{\"name\":\"Hello world\"}");
    transport.index_document(session, index, type, "id-2", "{\"name\":\"Hello nothing\"}");

    /* explicitly refresh the new index */
    transport.refresh(session, index);

    /* search the new index */
    transport.search(session, index, type, query);

    /* do something with the result */
    fprintf(stdout, "Response: %s\nLength: %zu\n", session->response.buffer, session->response.pos);
   
    /* drop the new index */
    transport.delete_index(session, index);

    transport.destroy(session);
    return 0;
}
```

Compile with: `-I<path to libcurl include> -L<path to libcurl> -lcurl` and `-I<path to libconfig include> -L<path to libconfig> -lconfig`
