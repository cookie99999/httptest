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
  if (remaining > 0) {
    if ((*buf = realloc(*buf, *bufsize += remaining)) == NULL)
      return -1;
    while (remaining > 0) {
      if ((n = recv(s, *buf + *total, remaining, 0)) < 0)
	return -1;
      *total += n;
      remaining -= n;
    }
  }
  return n < 0 ? -1 : 0;
}

int chunked_load(int s, char **buf, int *total, char *bodystart, int *bufsize) {
  printf("Loading chunks...\n");
  char *chunkbuf, *pos;
  int remaining;
  int chunkbufsize = CHUNKBUF_INIT_SIZE;
  long chunksize;
  if ((chunkbuf = malloc(CHUNKBUF_INIT_SIZE)) == NULL)
    return -1;

  /* We must deal with the first chunk separately as it's almost
     definitely been partially loaded while getting headers
  */
  memcpy(chunkbuf, bodystart, (*buf + *bufsize) - bodystart);
  chunksize = strtol(chunkbuf, &pos, 16);
  *total += chunksize;
  printf("First chunk size is %lx\n", chunksize);
  
  if (chunksize > chunkbufsize) {
    printf("dbg first chunk bigger than buffer\n");
    if ((chunkbuf = realloc(chunkbuf, chunkbufsize += chunksize)) == NULL)
      return -1;
  }
  
  if (chunksize > (*buf + *bufsize) - bodystart) {
    printf("dbg need to get rest of chunk\n");
    printf("dbg already have %lx\n", (*buf + *bufsize) - bodystart);
    remaining = (chunksize - ((*buf + *bufsize) - bodystart));
    printf("dbg remaining = %x/%lx\n", remaining, chunksize);
  }

  free(chunkbuf);
  return 0;
}

int recvall_http(int s, char **buf, int *count) {
  int total = 0;
  long remaining;
  int bufsize = RECVBUF_INIT_SIZE;
  int n;

  if ((*buf = malloc(bufsize)) == NULL)
    return -1;

  // get first chunk and then load the entire header
  if ((n = recv(s, *buf, bufsize, 0)) < 0)
    return -1;
  total += n;

  char *bodystart;
  //TODO: what happens if the server never puts a crlf crlf anywhere at all
  while ((bodystart = strstr(*buf, "\r\n\r\n")) == NULL) {
    if ((*buf = realloc(*buf, bufsize += REALLOC_INCR)) == NULL)
      return -1;
    if ((n = recv(s, *buf + total, bufsize, 0)) < 0)
      return -1;
    total += n;
  }
  bodystart += strlen("\r\n\r\n");
  long headerlen = (long)(bodystart - *buf);
  char *clstart;

  if (strstr(*buf, "Transfer-Encoding: chunked") != NULL) {
    n = chunked_load(s, buf, &total, bodystart, &bufsize);
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
  char ipstr[INET6_ADDRSTRLEN];
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
