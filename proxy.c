#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void Transaction(int fd);
void *thread_transaction(void *vargp);

int main(int argc, char **argv) { 
  int listenfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) { 
    clientlen = sizeof(clientaddr);
    int *connfd_ptr = (int *)Malloc(sizeof(int));
    *connfd_ptr = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    // Transaction(connfd); // iterator server
    Pthread_create(&tid, NULL, thread_transaction, connfd_ptr);
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
  char buf[MAXLINE], body[MAX_OBJECT_SIZE];
  int body_size;

  memset(body, 0, MAX_OBJECT_SIZE);
  /* build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Hikawa Proxy</em>\r\n", body);
  body_size = strlen(body);
 
  /* Print the HTTP response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", body_size);
  Rio_writen(fd, buf, strlen(buf));

  Rio_writen(fd, body, body_size);

  Close(fd);
}

// parse_uri - parse URI into scheme://<host>:<port><abs_path>
//              return 0 if success, 1 if failure
int parse_uri(const char *uri, char *host, char *port, char *abs_path) { 
  // default port 80
  strncpy(port, "80\0", 3);

  char *it = strstr(uri, "://");
  if (it != NULL) { 
    // an absoluteURI
    it += 3;
    char *end = strstr(it, "/");
    if (end == NULL) { strncpy(host, it, strlen(it)+1); }
    else { strncpy(host, it, (size_t)(end-it)); }

    it = strstr(host, ":");
    if (it != NULL) { 
      memset(port, 0, sizeof(char)*10);
      strncpy(port, it+1, 10); 
      memset(it, 0, sizeof(char)*strlen(it));
    }

    it = end;
  } else { 
    it = strstr(uri, "/"); 
    if (it == NULL) { return 1; }
  }

  if (it == NULL) { strncpy(abs_path, "/\0", 2); }
  else { strncpy(abs_path, it, strlen(it)+1); }

  return 0;
}

void Transaction(int fd) { 
  int clientfd;
  rio_t server_rio, client_rio;
  char buf[MAXLINE]; 
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE-9], port[10], abs_path[MAXLINE-16];
  sigset_t mask;

  Sigemptyset(&mask);
  Sigaddset(&mask, SIGPIPE);
  Sigprocmask(SIG_BLOCK, &mask, NULL);

  Rio_readinitb(&server_rio, fd);
  // read Request-Line
  if (Rio_readlineb(&server_rio, buf, MAXLINE) < 1) { 
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

  clientfd = Open_clientfd(host, port);
  Rio_readinitb(&client_rio, clientfd);
  sprintf(buf, "GET %s HTTP/1.0\r\n", abs_path);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Close(fd); return;
  }
  // send out Request-Header
  sprintf(buf, "Host: %s\r\n", host);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Close(fd); return;
  }

  sprintf(buf, "%sConnection: close\r\nProxy-Connection: close\r\n", user_agent_hdr);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Close(fd); return;
  }

  // forward additional request headers
  do {  
    if (Rio_readlineb(&server_rio, buf, MAXLINE) < 1
        || rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
      Close(clientfd); Close(fd); return;
    }
  } while (strcmp(buf, "\r\n"));

  long content_length;
  // send back response
  do {
    if (Rio_readlineb(&client_rio, buf, MAXLINE) < 1
        && rio_writen(fd, buf, strlen(buf)) != strlen(buf)) { 
      Close(clientfd); Close(fd); return;
    }

    if (!strncmp(buf, "Content-length: ", 16)) { 
      content_length = strtol(buf+16, NULL, 0);
    }
  } while (strcmp(buf, "\r\n"));

  char *tmp = (char *)Malloc(sizeof(char)*content_length);
  if (Rio_readnb(&client_rio, tmp, content_length) > 0) { 
    rio_writen(fd, tmp, content_length);
  }
  Free(tmp);

  Close(clientfd);
  Close(fd);
}

void *thread_transaction(void *vargp) { 
  int fd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);

  Transaction(fd);
  return NULL;
}

