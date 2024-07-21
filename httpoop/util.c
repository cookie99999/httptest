#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int split_uri(char *uri, char **scheme, char **host, char **resource) {
  /* scheme, host, and resource are pointers managed by the caller.
     they must be freed by the caller when no longer in use.
  */
  if (uri == NULL)
    return -1;

  int n = 0;
  int m = 0;

  if ((strstr(uri, "://")) == NULL) {
    if (scheme != NULL) {
      if ((*scheme = malloc(1)) == NULL)
	return -1;
      (*scheme)[0] = '\0';
    }
    goto noscheme;
  }
  
  for (int slashflag = 0; uri[m] != '\0' && slashflag != 2; m++) {
    if (uri[m] == '/')
      slashflag++;
  }

  if (scheme != NULL) {
    if ((*scheme = malloc(m + 1)) == NULL)
      return -1;
    memcpy(*scheme, uri, m);
    (*scheme)[m] = '\0';
    for (int i = 0; (*scheme)[i] != '\0'; i++) {
      (*scheme)[i] = tolower((*scheme)[i]);
    }
  }
  n += m;

 noscheme:
  
  for (m = 0; uri[n + m] != '\0' && uri[n + m] != '/'; m++) {
  }

  if (host != NULL) {
    if ((*host = malloc(m + 1)) == NULL)
      return -1;
    memcpy(*host, uri + n, m);
    (*host)[m] = '\0';
  }
  n += m;

  for (m = 0; uri[n + m] != '\0' && uri[n + m] != '?'; m++) {
  }

  if (resource != NULL) {
    if ((*resource = malloc(m + 1)) == NULL)
      return -1;
    memcpy(*resource, uri + n, m);
    (*resource)[m] = '\0';
  }

  return 0;
}
  
