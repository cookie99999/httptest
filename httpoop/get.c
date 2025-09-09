#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include "get.h"

#define BUF_INIT_SIZE 1024
#define REALLOC_INCR 1024

/* public domain macros from gnutls manual */
#define CHECK(x) assert((x) >= 0)
#define LOOP_CHECK(rval, cmd) \
  do {\
    rval = cmd;\
  } while (rval == GNUTLS_E_AGAIN || rval == GNUTLS_E_INTERRUPTED);\
  assert(rval >= 0)

void httpoop_response_delete(httpoop_response resp) {
  if (resp.buffer != NULL)
    free(resp.buffer);

  if (resp.charset != NULL)
    free(resp.charset);

  if (resp.content_type != NULL)
    free(resp.content_type);

  if (resp.redirect_uri != NULL)
    free(resp.redirect_uri);
}

/* Public domain function from beej.us/guide/bgnet */
static int sendall(int s, char *buf, int *len) {
  int total = 0;
  int bytesleft = *len;
  int n;

  while (total < *len) {
    n = send(s, buf + total, bytesleft, 0);
    if (n < 0)
      break;
    total += n;
    bytesleft -= n;
  }

  *len = total;

  return n < 0 ? -1 : 0;
}

/* Load response when Content-Length is provided by server */
static int cl_load(int s, gnutls_session_t session, char **buf, int *total, long remaining, int bufsize) {
  int n;
  char *tmp;

  if (remaining > 0) {
    if ((tmp = realloc(*buf, bufsize += remaining + 1)) == NULL)
      return -1;
    else
      *buf = tmp;
    while (remaining > 0) {
      if (s < 0) {
	LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, remaining));
      } else {
	n = recv(s, *buf + *total, remaining, 0);
      }
      if (n < 0)
        return -1;
      *total += n;
      remaining -= n;
    }
  }
  (*buf)[*total - 1] = '\0'; //terminate
  return n < 0 ? -1 : 0;
}

/* Load response when Transfer-Encoding: chunked is used by server */
static int chunked_load(int s, gnutls_session_t session, char **buf, int *total, char **bodystart, int bufsize) {
  char *chunkbuf = NULL, *pos = NULL, *tmp = NULL;
  int chunkbufsize = BUF_INIT_SIZE;
  int chunksize, remaining, data_recvd = 0, n;

  printf("Loading chunks...\n");

  if ((chunkbuf = malloc(BUF_INIT_SIZE)) == NULL)
    return -1;

  pos = *bodystart;
  /* At start of loop:
     pos points into buf at the beginning of the chunk size
     data_recvd = total actual chunk data received so far
  */
  for (;;) {
    chunksize = strtol(pos, &pos, 16); //pos = end of chunksize
    if (chunksize == 0)
      break; //last chunk
    
    if ((pos = strstr(pos, "\r\n")) == NULL) {
      fprintf(stderr, "chunked_load: malformed message body received from host\n");
      return -1;
    }
    pos += strlen("\r\n"); //pos now points at beginning of chunk data
    
    remaining = (chunksize - ((*buf + *total) - pos));
    if (remaining > 0) {
      data_recvd += (*buf + *total) - pos;
    } else {
      data_recvd += chunksize;
    }

    if (remaining > (bufsize - *total)) {
      //need more space in main buffer for remainder of chunk
      int tmpoffs1 = *bodystart - *buf;
      int tmpoffs2 = pos - *buf;
      if ((tmp = realloc(*buf, bufsize += remaining + 1)) == NULL)
	return -1;
      *buf = tmp;
      *bodystart = (*buf) + tmpoffs1;
      pos = (*buf) + tmpoffs2;
    }
  
    while (remaining > 0) {
      assert(remaining <= bufsize - *total);
      if (s < 0) {
	LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, remaining));
      } else {
	n = recv(s, *buf + *total, remaining, 0);
      }
      if (n < 0)
	return -1;
      *total += n;
      remaining -= n;
      data_recvd += n;
    }

    while (data_recvd > chunkbufsize) {
      //need more space in chunk buffer before copying
      if ((tmp = realloc(chunkbuf, chunkbufsize += REALLOC_INCR)) == NULL)
	return -1;
      chunkbuf = tmp;
    }
	
    memcpy(chunkbuf + (data_recvd - chunksize), pos, chunksize);
    pos += chunksize;
    pos += strlen("\r\n"); //terminating crlf on chunk

    //get start of next chunk and go again
    int tmpoffs1 = *bodystart - *buf;
    int tmpoffs2 = pos - *buf;
    if ((tmp = realloc(*buf, bufsize += REALLOC_INCR + 1)) == NULL)
      return -1;
    *buf = tmp;
    *bodystart = (*buf) + tmpoffs1;
    pos = (*buf) + tmpoffs2;

    remaining = REALLOC_INCR;
    while (remaining > 0) {
      if (s < 0) {
	LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, remaining));
      } else {
	n = recv(s, *buf + *total, remaining, 0);
      }
      if (n < 0)
	return -1;
      *total += n;
      remaining -= n;
      if (strstr(*buf + (*total - n), "\r\n\r\n")) //end of message
	break;
    }

    assert(pos < (*buf + bufsize) && pos >= *buf);
    assert(*bodystart < (*buf + bufsize) && *bodystart >= *buf);
    assert(chunkbufsize >= data_recvd);
    assert(bufsize >= *total);
  }
  
  memcpy(*bodystart, chunkbuf, data_recvd);
  (*buf)[(*bodystart - *buf) + data_recvd] = '\0'; //terminate properly and cut off trailer etc
  free(chunkbuf);
  return 0;
}

static int recvall_http(int s, gnutls_session_t session, char **buf, int *count) {
  int total = 0;
  long remaining;
  int bufsize = BUF_INIT_SIZE;
  int n;
  char *tmp;

  if ((*buf = malloc(bufsize)) == NULL)
    return -1;

  /* get a first piece of data, and then
     continue loading until we get CRLFCRLF
     signifying end of header
  */
  if (s < 0) {
    LOOP_CHECK(n, gnutls_record_recv(session, *buf, bufsize));
  } else {
    n = recv(s, *buf, bufsize, 0);
  }
  if (n < 0)
    return -1;
  total += n;

  char *bodystart;
  while ((bodystart = strstr(*buf, "\r\n\r\n")) == NULL) {
    int tmpoffs = bodystart - *buf;
    if ((tmp = realloc(*buf, bufsize += REALLOC_INCR)) == NULL)
      return -1;
    *buf = tmp;
    bodystart = (*buf) + tmpoffs;

    if (s < 0) {
      LOOP_CHECK(n, gnutls_record_recv(session, *buf + total, REALLOC_INCR));
    } else {
      n = recv(s, *buf + total, REALLOC_INCR, 0);
    }
    if (n < 0)
      return -1;
    total += n;
  }
  bodystart += strlen("\r\n\r\n");
  long headerlen = (long)(bodystart - *buf);
  char *clstart;

  if (strcasestr(*buf, "Transfer-Encoding: chunked") != NULL) {
    n = chunked_load(s, session, buf, &total, &bodystart, bufsize);
  } else if ((clstart = strcasestr(*buf, "Content-Length: ")) != NULL) {
    clstart += strlen("Content-Length: ");
    long cl = strtol(clstart, NULL, 10);
    remaining = (cl - (total - headerlen));
    n = cl_load(s, session, buf, &total, remaining, bufsize);
  } else {
    /* http 1.1 spec says some servers can just keep sending
       data until they close the connection,
       but i haven't seen this yet and am lazy
       so i'll leave it for now
    */
    printf("Remote host did not provide content length or supported transfer encoding.\n");
    n = -1;
  }
  
  *count = total;
  return n < 0 ? -1 : 0;
}

static void strip_headers(char *buf, int len) {
  char *bodystart = strstr(buf, "\r\n\r\n");
  /* TODO: replace this with actual error handling because 404s etc often
     have empty bodies
  */
  assert(bodystart != NULL);
  bodystart += strlen("\r\n\r\n");
  int bodylen = len - (bodystart - buf);
  memmove(buf, bodystart, bodylen);
}

void parse_headers(httpoop_response *resp) {
  //status
  char *pos = strstr(resp->buffer, "HTTP/1.1 ");
  assert(pos != NULL);
  pos += strlen("HTTP/1.1 ");
  resp->status = (int) strtol(pos, NULL, 10); //TODO bad typecast

  //header length
  pos = strstr(resp->buffer, "\r\n\r\n");
  assert(pos != NULL);
  pos += strlen("\r\n\r\n");
  resp->header_length = pos - resp->buffer;

  //redirect if applicable
  pos = strcasestr(resp->buffer, "Location: ");
  if (pos != NULL) {
    pos += strlen("Location: ");
    int n;
    for (n = 0; pos[n] != '\0' && pos[n] != '\r'; n++)
      ;
    if (n > 0) {
      if ((resp->redirect_uri = malloc(n + 1)) == NULL)
	exit(-1);
      memcpy(resp->redirect_uri, pos, n);
      resp->redirect_uri[n] = '\0';
    }
  }

  //content type
  pos = strcasestr(resp->buffer, "Content-Type: ");
  if (pos != NULL) {
    pos += strlen("Content-Type: ");
    int n;
    for (n = 0; pos[n] != '\r' && pos[n] != ';' && pos[n] != '\0'; n++)
      ;
    if (n > 0) {
      if ((resp->content_type = malloc(n + 1)) == NULL)
	exit(-1);
      memcpy(resp->content_type, pos, n);
      resp->content_type[n] = '\0';
    }

    //charset
    if (pos[n] == ';') {
      pos = strcasestr(pos + n, "charset=");
      if (pos != NULL && pos < (resp->buffer + resp->header_length)) {
	pos += strlen("charset=");
	for (n = 0; pos[n] != '\r' && pos[n] != ';' && pos[n] != '\0'; n++)
	  ;
	if (n > 0) {
	  if ((resp->charset = malloc(n + 1)) == NULL)
	    exit(-1);
	  memcpy(resp->charset, pos, n);
	  resp->charset[n] = '\0';
	}
      }
    }
  }
}
  
httpoop_response httpoop_get(char *scheme, char *host, char *resource) {
  struct addrinfo hints, *res;
  int status, s, s2, bytecount, secure;
  char reqbuf[BUF_INIT_SIZE];
  char *port;
  HTTPOOP_RESPONSE_NEW(resp);
  gnutls_session_t session;
  gnutls_datum_t out;
  gnutls_certificate_credentials_t xcred;

  if (scheme[0] == '\0' || strcasecmp(scheme, "https://") == 0)
    secure = 1;
  else if (strcasecmp(scheme, "http://") == 0)
    secure = 0;
  else {
    fprintf(stderr, "Protocol scheme %s not supported.\n", scheme);
    return resp;
  }
  port = secure == 1 ? "443" : "80";

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  printf("Looking up %s...\n", host);
  if ((status = getaddrinfo(host, port, &hints, &res)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    return resp;
  }

  if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
    perror("socket");
    return resp;
  }

  printf("Connecting to %s...\n", host);
  if ((connect(s, res->ai_addr, res->ai_addrlen)) < 0) {
    perror("connect");
    return resp;
  }

  freeaddrinfo(res);

  if (secure == 1) {
    if (gnutls_check_version("3.4.6") == NULL) {
      fprintf(stderr,
	      "GnuTLS 3.4.6 or later is required\n");
      exit(1);
    }

    CHECK(gnutls_global_init());
    CHECK(gnutls_certificate_allocate_credentials(&xcred));
    CHECK(gnutls_certificate_set_x509_system_trust(xcred));
    CHECK(gnutls_init(&session, GNUTLS_CLIENT));
    CHECK(gnutls_server_name_set(session, GNUTLS_NAME_DNS, host, strlen(host)));
    CHECK(gnutls_set_default_priority(session));
    CHECK(gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, xcred));
    gnutls_session_set_verify_cert(session, host, 0);

    gnutls_transport_set_int(session, s);
    gnutls_handshake_set_timeout(session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

    printf("Handshaking with %s...\n", host);
    do {
      status = gnutls_handshake(session);
    } while (status < 0 && gnutls_error_is_fatal(status) == 0);
    if (status < 0) {
      if (status == GNUTLS_E_CERTIFICATE_VERIFICATION_ERROR) {
	int type = gnutls_certificate_type_get(session);
	int tmpstatus = gnutls_session_get_verify_cert_status(session);
	CHECK(gnutls_certificate_verification_status_print(tmpstatus, type, &out, 0));
	printf("cert verify: %s\n", out.data);
	gnutls_free(out.data);
      }
      fprintf(stderr, "handshake failed %s\n", gnutls_strerror(status));
      goto end;
    } else {
      char *desc = gnutls_session_get_desc(session);
      printf("Handshake successful: %s\n", desc);
      gnutls_free(desc);
    }

    s2 = s;
    s = -1;
  }
  
  int n;
  //TODO: size this buffer dynamically in case you have a really big header
  if ((n = snprintf(reqbuf, BUF_INIT_SIZE, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: gtktest/0.01 libhttpoop/0.01\r\n\r\n", resource, host)) < 0) {
    fprintf(stderr, "snprintf: encoding error\n");
    return resp;
  }

  printf("Sending request for %s...\n", resource);
  if (secure == 1) {
    LOOP_CHECK(status, gnutls_record_send(session, reqbuf, strlen(reqbuf)));
  } else {
    if ((bytecount = sendall(s, reqbuf, &n)) < 0) {
      perror("sendall");
      return resp;
    }
  }

  printf("Waiting for response... ");
  if ((recvall_http(s, session, &(resp.buffer), &bytecount)) < 0) {
    perror("recv");
    return resp;
  }

  if (bytecount == 0) {
    printf("Connection closed by remote host.\n");
    return resp;
  } else if (bytecount < 0 && secure == 1 && gnutls_error_is_fatal(bytecount) == 0) {
    fprintf(stderr, "GnuTLS warning: %s\n", gnutls_strerror(bytecount));
  } else if (bytecount < 0 && secure == 1) {
    fprintf(stderr, "GnuTLS error: %s\n", gnutls_strerror(bytecount));
    goto end;
  }

  printf("Bytes received: %d\n", bytecount);
  resp.length = bytecount;
  parse_headers(&resp);
  if (resp.status == 200) //todo store headers in a separate buffer instead of just throwing them away
    strip_headers(resp.buffer, bytecount + 1); //+1 for \0

  if (secure == 1) {
    CHECK(gnutls_bye(session, GNUTLS_SHUT_RDWR));
    s = s2;
  }

 end:
  shutdown(s, SHUT_RDWR);
  close(s);
  if (secure == 1) {
    gnutls_deinit(session);
    gnutls_certificate_free_credentials(xcred);
    gnutls_global_deinit();
  }
  printf("Connection closed.\n");

  return resp;
}
