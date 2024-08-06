#ifndef __GET_H
#define __GET_H

struct _httpoop_response {
  char *buffer;
  int status;
  int length;
  int header_length;
  char *redirect_uri;
  char *content_type;
  char *charset;
};

typedef struct _httpoop_response httpoop_response;
#define HTTPOOP_RESPONSE_NEW(x) httpoop_response x = { .buffer = NULL, .redirect_uri = NULL, .content_type = NULL, .charset = NULL }
void httpoop_response_delete(httpoop_response resp);

void parse_headers(httpoop_response *resp);
httpoop_response httpoop_get(char *scheme, char *host, char *resource);

#endif /* __GET_H */
