#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define REQBUF_SIZE 1024
#define RECVBUF_INIT_SIZE 1024
#define REALLOC_INCR 1024
#define CHUNKBUF_INIT_SIZE 1024

//TODO: all pointers into buffers need to be recomputed safely when realloc'ing'

/* Public domain function from beej.us/guide/bgnet */
int sendall(int s, char *buf, int *len) {
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

int cl_load(int s, char **buf, int *total, long remaining, int *bufsize) {
  int n;
  char *tmp;

  if (remaining > 0) {
    if ((tmp = realloc(*buf, *bufsize += remaining + 1)) == NULL)
      return -1;
    else
      *buf = tmp;
    (*buf)[*bufsize - 1] = '\0'; //re-terminate
    while (remaining > 0) {
      if ((n = recv(s, *buf + *total, remaining, 0)) < 0)
        return -1;
      *total += n;
      remaining -= n;
    }
  }
  return n < 0 ? -1 : 0;
}

/* s: socket used for receiving
 * **buf: pointer to character buffer, the final full message will go here
 * *total: pointer to running total of bytes received into buf
 * **bodystart: pointer to pointer to beginning of message body in buf
 * *bufsize: pointer to total allocated size of buf
 */
int chunked_load(int s, char **buf, int *total, char **bodystart, int *bufsize) {
  char *chunkbuf, *pos, *tmp;
  int chunkbufsize = CHUNKBUF_INIT_SIZE;
  int chunksize, remaining, data_recvd, n;
  /* *chunkbuf: temporary buffer used for decoded chunk data
   * *pos: temporary pointer to our current decoding position in buf
   * *tmp: temporary pointer for reallocs
   * chunkbufsize: size of chunkbuf
   * chunksize: size of currently decoding chunk
   * remaining: bytes left to receive of currently decoding chunk
   * data_recvd: bytes decoded into chunkbuf
   * n: temp variable for counting bytes received
   */

  printf("Loading chunks...\n");

  if ((chunkbuf = malloc(CHUNKBUF_INIT_SIZE)) == NULL)
    return -1;

  pos = *bodystart;
  //TODO: make this a loop once it works
  chunksize = strtol(pos, &pos, 16); //pos = end of chunksize
  if ((pos = strstr(pos, "\r\n")) == NULL) {
    fprintf(stderr, "chunked_load: malformed message body received from host (or foolish programming error)\n");
    return -1;
  }
  pos += strlen("\r\n"); //pos now points at beginning of chunk data
  printf("First chunk size is 0x%x\n", chunksize);

  data_recvd = (*buf + *bufsize) - pos;
  printf("dbg already have 0x%lx\n", (*buf + *bufsize) - pos);
  remaining = (chunksize - ((*buf + *bufsize) - pos));
  printf("dbg remaining = 0x%x/0x%x\n", remaining, chunksize);

  if (chunksize > (chunkbufsize - data_recvd)) {
    printf("dbg first chunk bigger than chunk buffer\n");
    if ((tmp = realloc(chunkbuf, chunkbufsize += remaining)) == NULL)
      return -1;
    chunkbuf = tmp;
  }

  if (chunksize > (*bufsize - *total)) {
    printf("dbg chunk bigger than main buffer\n");
    int tmpoffs1 = *bodystart - *buf;
    int tmpoffs2 = pos - *buf;
    if ((tmp = realloc(*buf, *bufsize += remaining)) == NULL)
      return -1;
    *buf = tmp;
    *bodystart = (*buf) + tmpoffs1;
    pos = (*buf) + tmpoffs2;
  }

  if (chunksize > data_recvd) {
    printf("dbg need to get rest of chunk\n");
    while (remaining > 0) {
      if ((n = recv(s, *buf, remaining, 0)) < 0)
        return -1;
      *total += n;
      data_recvd += n;
      remaining -= n;
    }
  }

  memcpy(chunkbuf, pos, chunksize);

  memcpy(*bodystart, chunkbuf, data_recvd);

  free(chunkbuf);
  return 0;
}

int recvall_http(int s, char **buf, int *count) {
  int total = 0;
  long remaining;
  int bufsize = RECVBUF_INIT_SIZE;
  int n;
  char *tmp;

  if ((*buf = malloc(bufsize + 1)) == NULL)
    return -1;
  memset(*buf, '\0', bufsize + 1); //terminate

  // get first chunk and then load the entire header
  if ((n = recv(s, *buf, bufsize, 0)) < 0)
    return -1;
  total += n;

  char *bodystart;
  //TODO: what happens if the server never puts a crlf crlf anywhere at all
  while ((bodystart = strstr(*buf, "\r\n\r\n")) == NULL) {
    int tmpoffs = bodystart - *buf;
    if ((tmp = realloc(*buf, bufsize += REALLOC_INCR + 1)) == NULL)
      return -1;
    *buf = tmp;
    bodystart = (*buf) + tmpoffs;
    (*buf)[bufsize - 1] = '\0'; //re-terminate

    if ((n = recv(s, *buf + total, REALLOC_INCR, 0)) < 0)
      return -1;
    total += n;
  }
  bodystart += strlen("\r\n\r\n");
  long headerlen = (long)(bodystart - *buf);
  char *clstart;

  if (strstr(*buf, "Transfer-Encoding: chunked") != NULL) {
    n = chunked_load(s, buf, &total, &bodystart, &bufsize);
  } else if ((clstart = strstr(*buf, "Content-Length: ")) != NULL) {
    clstart += strlen("Content-Length: ");
    long cl = strtol(clstart, NULL, 10);
    remaining = (cl - (bufsize - headerlen));
    n = cl_load(s, buf, &total, remaining, &bufsize);
  } else {
    //TODO: receive until connection closed
  }
  
  *count = total;
  return n < 0 ? -1 : 0;
}
  
int main(int argc, char **argv) {
  struct addrinfo hints, *res;
  int status, s, bytecount;
  char reqbuf[REQBUF_SIZE];
  char *recvbuf;

  if (argc != 3) {
    fprintf(stderr, "Usage: gettest hostname resource\n");
    return 1;
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  printf("Looking up %s...\n", argv[1]);
  if ((status = getaddrinfo(argv[1], "80", &hints, &res)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    return 2;
  }

  if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
    perror("socket");
    return 3;
  }

  printf("Connecting to %s...\n", argv[1]);
  if ((connect(s, res->ai_addr, res->ai_addrlen)) < 0) {
    perror("connect");
    return 4;
  }

  freeaddrinfo(res);
  
  int n;
  if ((n = snprintf(reqbuf, REQBUF_SIZE, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", argv[2], argv[1])) < 0) {
    fprintf(stderr, "snprintf: encoding error\n");
    return 5;
  }

  printf("Sending request for %s...\n", argv[2]);
  if ((bytecount = sendall(s, reqbuf, &n)) < 0) {
    perror("sendall");
    return 6;
  }

  printf("Waiting for response... ");
  if ((recvall_http(s, &recvbuf, &bytecount)) < 0) {
    perror("recv");
    return 7;
  }

  if (bytecount == 0) {
    printf("Connection closed by remote host.\n");
    return 0;
  }

  printf("Bytes received: %d\n", bytecount);

  printf("%s\n", recvbuf);
  free(recvbuf);
  close(s);
  printf("Connection closed.\n");
  return 0;
}
