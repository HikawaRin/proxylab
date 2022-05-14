#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

// cache part
typedef struct {
  int cached;
  char *host;
  char *abs_path;
  char *type;
  long size;
  char *data;
}cache_item;

typedef struct node { 
  cache_item *item;
  struct node *prev, *next;
}node;

struct { 
  int readcnt;
  sem_t mutex, write;
  int free_space;
  node *head;
}cache;

// cache method
void put_node(node *n, node *p);
node *remove_node(node *n);

void init_cache();
cache_item *query_cache(rio_t server_rio, const char *host, const char *port, 
    const char *abs_path);
// end cache

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

  init_cache();

  listenfd = Open_listenfd(argv[1]);
  while (1) { 
    clientlen = sizeof(clientaddr);
    int *connfd_ptr = (int *)Malloc(sizeof(int));
    *connfd_ptr = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    // Transaction(*connfd_ptr); // iterator server
    // Free(connfd_ptr);
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

// put_node - put node n at the front of p
void put_node(node *n, node *p) { 
  if (p != NULL && p->prev != NULL) { p->prev->next = n; }
  if (p != NULL) { n->prev = p->prev; }
  if (p != NULL) { p->prev = n; }
  n->next = p;
}

node *remove_node(node *n) { 
  if (n->prev != NULL) { n->prev->next = n->next; }
  if (n->next != NULL) { n->next->prev = n->prev; }
  n->prev = NULL;
  n->next = NULL;

  return n;
}

void init_cache() { 
  cache.readcnt = 0;
  sem_init(&cache.mutex, 0, 1);
  sem_init(&cache.write, 0, 1);
  cache.free_space = MAX_CACHE_SIZE;
  cache.head = NULL;
}

void Free_cacheitem(cache_item *item) { 
  if (item == NULL) { return; }
  Free(item->host);
  Free(item->abs_path);
  if (item->type != NULL) { Free(item->type); }
  if (item->size > 0) { Free(item->data); }
  
  Free(item);
  item = NULL;
}

void evicate_node(node *n) { 
  if (n == NULL) { return; }
  Free_cacheitem(n->item);
  Free(n);
  n = NULL;
}

// update_cache - Update cache content, return received HTTP response
//                NULL if failed
cache_item *update_cache(rio_t server_rio, const char *host, char *port, 
    const char *abs_path) { 
  int clientfd, tmp;
  rio_t client_rio;
  char buf[MAXLINE];
  cache_item *item_ptr = (cache_item *)Malloc(sizeof(cache_item));

  // Initial cache_item
  item_ptr->cached = 0;
  tmp = strlen(host)+1;
  item_ptr->host = (char *)Malloc(sizeof(char) * tmp);
  strncpy(item_ptr->host, host, tmp);
  tmp = strlen(abs_path)+1;
  item_ptr->abs_path = (char *)Malloc(sizeof(char) * tmp);
  strncpy(item_ptr->abs_path, abs_path, tmp);
  item_ptr->type = NULL;
  item_ptr->size = 0;
  item_ptr->data = NULL;
  // end initial cache_item

  clientfd = Open_clientfd(host, port);
  Rio_readinitb(&client_rio, clientfd);

  sprintf(buf, "GET %s HTTP/1.0\r\n", abs_path);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Free_cacheitem(item_ptr); return NULL;
  }

  // send out Request-Header
  sprintf(buf, "Host: %s\r\n", host);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Free_cacheitem(item_ptr); return NULL;
  }
  sprintf(buf, 
      "%sConnection: close\r\nProxy-Connection: close\r\n", user_agent_hdr);
  if (rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
    Close(clientfd); Free_cacheitem(item_ptr); return NULL;
  }

  // forward additional request headers
  do {  
    if (Rio_readlineb(&server_rio, buf, MAXLINE) < 1
        || rio_writen(clientfd, buf, strlen(buf)) != strlen(buf)) { 
      Close(clientfd); Free_cacheitem(item_ptr); return NULL;
    }
  } while (strcmp(buf, "\r\n"));

  // cache response
  do {
    if (Rio_readlineb(&client_rio, buf, MAXLINE) < 1) { 
      Close(clientfd); Free_cacheitem(item_ptr); return NULL;
    }

    if (!strncmp(buf, "Content-type: ", 14)) { 
      tmp = strlen((buf + 14)) + 1;
      item_ptr->type = (char *)Malloc(sizeof(char) * tmp);
      strncpy(item_ptr->type, (buf + 14), tmp);
    }
    if (!strncmp(buf, "Content-length: ", 16)) { 
      item_ptr->size = strtol(buf+16, NULL, 0);
    }
  } while (strcmp(buf, "\r\n"));

  if (item_ptr->size <= MAX_OBJECT_SIZE) { item_ptr->cached = 1; }

  item_ptr->data = (char *)Malloc(sizeof(char) * item_ptr->size);
  if (Rio_readnb(&client_rio, item_ptr->data, item_ptr->size) < 1) { 
    Close(clientfd); Free_cacheitem(item_ptr); return NULL;
  }
  Close(clientfd); 
  
  if (item_ptr->cached) { 
    P(&cache.write);
    if (cache.free_space < item_ptr->size) { 
      // evicte cache
      node *tail = cache.head;
      while (tail->next != NULL) { tail = tail->next; }
      while (tail != NULL && cache.free_space < item_ptr->size) { 
        node *victim = tail;
        cache.free_space += victim->item->size;
        tail = tail->prev;
        evicate_node(remove(victim));
      }
    }
    cache.free_space -= item_ptr->size;
    node *n = (node *)Malloc(sizeof(node));
    n->next = n->prev = NULL;
    n->item = item_ptr;
    put_node(n, cache.head);
    cache.head = n;
    V(&cache.write);
  }
  return item_ptr;
}

cache_item *query_cache(rio_t server_rio, const char *host, const char *port, 
    const char *abs_path) { 
  cache_item *item = NULL;
  
  P(&cache.mutex);
  ++cache.readcnt;
  if (cache.readcnt == 1) { P(&cache.write); }
  
  node *it = cache.head;
  while (it != NULL) { 
    if (!strcmp(it->item->host, host)
        && !strcmp(it->item->abs_path, abs_path)) { 
      it = remove_node(it);
      put_node(it, cache.head);
      cache.head = it;
      item = it->item;
      break;
    }
    it = it->next;
  }

  --cache.readcnt;
  if (cache.readcnt == 0) { V(&cache.write); }
  V(&cache.mutex);
  
  if (item == NULL) { 
    // Update cache
    item = update_cache(server_rio, host, port, abs_path); 
  }

  return item;
}

void Transaction(int fd) { 
  rio_t server_rio;
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

  cache_item *response = query_cache(server_rio, host, port, abs_path); 
  if (response != NULL) { 
    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s", response->type);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %ld\r\n\r\n", response->size);
    Rio_writen(fd, buf, strlen(buf));

    Rio_writen(fd, response->data, response->size);

    if (!response->cached) { Free_cacheitem(response); }
  }

  Close(fd);
}

void *thread_transaction(void *vargp) { 
  int fd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  vargp = NULL;

  Transaction(fd);
  return NULL;
}

