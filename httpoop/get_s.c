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
#include "get_s.h"

/* public domain macros from gnutls manual */
#define CHECK(x) assert((x) >= 0)
#define LOOP_CHECK(rval, cmd) \
  do {\
    rval = cmd;\
  } while (rval == GNUTLS_E_AGAIN || rval == GNUTLS_E_INTERRUPTED);\
  assert(rval >= 0)

#define BUF_INIT_SIZE 1024
#define REALLOC_INCR 1024

/* Load response when Content-Length is provided by server */
static int cl_load(gnutls_session_t session, char **buf, int *total, long remaining, int bufsize) {
  int n;
  char *tmp;

  if (remaining > 0) {
    if ((tmp = realloc(*buf, bufsize += remaining + 1)) == NULL)
      return -1;
    else
      *buf = tmp;
    while (remaining > 0) {
      LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, remaining));
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
static int chunked_load(gnutls_session_t session, char **buf, int *total, char **bodystart, int bufsize) {
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
      LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, remaining));
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

    LOOP_CHECK(n, gnutls_record_recv(session, *buf + *total, REALLOC_INCR));
    if (n < 0)
      return -1;
    *total += n;

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

static int recvall_https(gnutls_session_t session, char **buf, int *count) {
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
  LOOP_CHECK(n, gnutls_record_recv(session, *buf, bufsize));
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

    LOOP_CHECK(n, gnutls_record_recv(session, *buf + total, REALLOC_INCR));
    if (n < 0)
      return -1;
    total += n;
  }
  bodystart += strlen("\r\n\r\n");
  long headerlen = (long)(bodystart - *buf);
  char *clstart;

  if (strstr(*buf, "Transfer-Encoding: chunked") != NULL) {
    n = chunked_load(session, buf, &total, &bodystart, bufsize);
  } else if ((clstart = strstr(*buf, "Content-Length: ")) != NULL) {
    clstart += strlen("Content-Length: ");
    long cl = strtol(clstart, NULL, 10);
    remaining = (cl - (total - headerlen));
    n = cl_load(session, buf, &total, remaining, bufsize);
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
  
httpoop_response httpoop_get_s(char *host, char *resource) {
  struct addrinfo hints, *res;
  int status, s, bytecount;
  char reqbuf[BUF_INIT_SIZE];
  HTTPOOP_RESPONSE_NEW(resp);
  gnutls_session_t session;
  gnutls_datum_t out;
  gnutls_certificate_credentials_t xcred;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  printf("Looking up %s...\n", host);
  if ((status = getaddrinfo(host, "443", &hints, &res)) != 0) {
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

  if (gnutls_check_version("3.4.6") == NULL) {
    fprintf(stderr,
	    "GnuTLS 3.4.6 or later is required for this example\n");
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
  
  int n;
  //TODO: size this buffer dynamically in case you have a really big header
  if ((n = snprintf(reqbuf, BUF_INIT_SIZE, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", resource, host)) < 0) {
    fprintf(stderr, "snprintf: encoding error\n");
    return resp;
  }

  printf("Sending request for %s...\n", resource);
  LOOP_CHECK(status, gnutls_record_send(session, reqbuf, strlen(reqbuf)));

  printf("Waiting for response... ");
  if ((recvall_https(session, &(resp.buffer), &bytecount)) < 0) {
    perror("recv");
    return resp;
  }

  if (bytecount == 0) {
    printf("Connection closed by remote host.\n");
    return resp;
  } else if (bytecount < 0 && gnutls_error_is_fatal(bytecount) == 0) {
    fprintf(stderr, "GnuTLS warning: %s\n", gnutls_strerror(bytecount));
  } else if (bytecount < 0) {
    fprintf(stderr, "GnuTLS error: %s\n", gnutls_strerror(bytecount));
    goto end;
  }

  printf("Bytes received: %d\n", bytecount);
  resp.length = bytecount;
  char *tmppos = strstr(resp.buffer, "HTTP/1.1 ");
  assert(tmppos != NULL);
  tmppos += strlen("HTTP/1.1 ");
  resp.status = (int) strtol(tmppos, NULL, 10); //TODO bad typecast
  printf("status = %d\n", resp.status);
  if (resp.status == 200)
    strip_headers(resp.buffer, bytecount + 1); //+1 for \0

  CHECK(gnutls_bye(session, GNUTLS_SHUT_RDWR));

 end:
  shutdown(s, SHUT_RDWR);
  close(s);
  gnutls_deinit(session);
  gnutls_certificate_free_credentials(xcred);
  gnutls_global_deinit();
  
  return resp;
}
