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
  struct sockaddr_storage clientaddr;

  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) { 
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    Transaction(connfd); 
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

// parse_uri - parse URI into scheme://<host>:<port><abs_path>
//              return 0 if success, 1 if failure
int parse_uri(const char *uri, char *host, char *port, char *abs_path) { 
  char *it = strstr(uri, "://");
  if (it != NULL) { 
    // an absoluteURI
    it += 3;
    char *end = strstr(it, "/");
    if (end == NULL) { strncpy(host, it, MAXLINE); }
    else { strncpy(host, it, (size_t)(end-it)); }

    it = strstr(host, ":");
    if (it != NULL) { 
      strncpy(port, it+1, MAXLINE); 
      *it = '\0';
    }

    it = end;
  } else { 
    it = strstr(uri, "/"); 
    if (it == NULL) { return 1; }
  }

  // port not specified, default 80
  if (!strcmp(port, "")) { strncpy(port, "80\0", 3); }

  if (it == NULL) { strncpy(abs_path, "/\0", 2); }
  else { strncpy(abs_path, it, MAXLINE); }

  return 0;
}

void Transaction(int fd) { 
  rio_t rio;
  char buf[MAXLINE]; 
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], abs_path[MAXLINE];

  Rio_readinitb(&rio, fd);
  // read Request-Line
  if (!Rio_readlineb(&rio, buf, MAXLINE)) { 
    Close(fd); return;
  }
  sscanf(buf, "%s %s %s", method, uri, version); // parse Request-Line
  if (strcasecmp(method, "GET")) { 
    proxy_error(fd, method, "501", 
        "Not Implemented", "Hikawa Proxy does not implement this method. ");
    return;
  }
  if (parse_uri(uri, host, port, abs_path)) { 
    proxy_error(fd, method, "400", 
        "Bad Request", "Request-URI broken. ");
    return; 
  }

}
