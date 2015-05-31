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
