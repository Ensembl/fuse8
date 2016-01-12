#ifndef HTTP_CONN_H
#define HTTP_CONN_H

struct connections;
struct connection;

#include "client.h"

typedef void (*conn_cb)(struct connection *conn,void *priv);

void unget_connection(struct connection *conn);

void get_connection(struct connections *cnn,
                    const char *host,int port,
                    conn_cb callback,void *priv);

struct evhttp_connection * evconnection(struct connection *conn);
struct connections * cnn_make(struct httpclient *cli);
void cnn_free(struct connections *cnn);

void cnn_stats(struct connections *cnn,int64_t *n_new,int64_t *dns_time);

#endif
