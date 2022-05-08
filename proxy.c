#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void Transaction(int fd);

int main(int argc, char **argv) { 
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddrs;

  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) { 
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    
  }

  return 0;
}

// proxy_error - returns error message to the client and close connection
//
// Params: 
//    fd - the connect descriptor
//    cause - which request caused the error
//    errnum - the http error number
//    shortmsg - http response phrase
//    longmsg - error detail
void proxy_error(int fd, char *cause, char *errnum,
    char *shortmsg, char *longmsg) { 
  char buf[MAXLINE];
  
  /* Print the HTTP response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* Print the HTTP response body */
  sprintf(buf, "<html><title>Proxy Error</title>");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<hr><em>The Hikawa Proxy</em>\r\n");
  Rio_writen(fd, buf, strlen(buf));

  Close(fd);
}
