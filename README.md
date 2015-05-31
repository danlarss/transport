# transport

Transport is a simple helper library for communicating with elastic.

##### Example usage:
*settings.cfg*
```
hosts = ({ host = "http://127.0.0.1";
           port = 9200; },
         { host = "http://127.0.0.1";
           port = 9210; }
         );
timeout = 1
```

*test.c*
```c
#include <stdio.h>
#include "transport/transport.h"

int main(int argc, char **argv) {
  
   int ret;
   char * query = "{\"query\":{\"filtered\":{\"query\":{\"match\":{\"name\":\"needle\"}}}}}";
   transport_head_t * head = transport_create("settings.cfg");
  
   ret = transport.search(head, "myindex", "mytype", query);
   if (ret == 0) {
      fprintf(stdout, "%s\n", head->response.ptr);
   } else {
      fprintf(stderr, "%s\n", transport.strerror(ret));
   }
   
   transport.destroy(head);
   return 0;
}
```

Compile with: `-I<path to libcurl include> -L<path to libcurl> -lcurl` and `-I<path to libconfig include> -L<path to libconfig> -lconfig`

## Functions

### transport.create

```c
transport_head_t * transport.create(const char * config);
```
Create and initialize a transport head struct.

**Parameters**
 - *config* Path to config file.

**Return**
 - A transport head struct.

### transport.destroy

```c
void transport.destroy(transport_head_t * head);
```
Free transport head struct.

**Parameters**
 - *head* Transport head struct.

### transport.search

```c
int transport.search(transport_head_t * head, const char * index, const char * type, const char * payload);
```
Perform an elastic search.

**Parameters**
 - *head* Transport head struct.
 - *index* Elastic index name
 - *type* Elastic document type name
 - *payload* HTTP POST body in JSON format

**Return**
 - 0 on success or a transport error code.

### transport.create_index

```c
int transport.create_index(transport_head_t * head, const char * index, const char * payload);
```
Create a new elastic index.

**Parameters**
 - *head* Transport head struct.
 - *index* Elastic index name
 - *payload* HTTP PUT body in JSON format

**Return**
 - 0 on success or a transport error code.

### transport.delete_index

```c
int transport.delete_index(transport_head_t * head, const char * index);
```
Delete an elastic index.

**Parameters**
 - *head* Transport head struct.
 - *index* Elastic index name
 
**Return**
 - 0 on success or a transport error code.

### transport.index_document

```c
int transport.index_document(transport_head_t * head, const char * index, const char * type, const char * id, const char * payload)
```
Index an elastic document.

**Parameters**
 - *head* Transport head struct.
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