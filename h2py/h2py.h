#ifndef _EXTERNAL_H_
#define _EXTERNAL_H_

#include <Python.h>
#include <uv.h>

#include <h2o.h>
#include <h2o/http1.h>
#include <h2o/http2.h>

#include <openssl/ssl.h>


typedef struct {
    // This is a description of `pyuv.Loop` objects.
    // A small part of it, actually. We only need the `uv_loop` field.
    PyObject_HEAD
    PyObject *weakreflist;
    PyObject *dict;
    uv_loop_t loop_struct;
    uv_loop_t *uv_loop;
} PyUV_Loop;


typedef struct {
    // Same story with PySSLContext objects.
    // https://hg.python.org/cpython/file/70a55b2dee71/Modules/_ssl.c#l184
    PyObject_HEAD
    SSL_CTX *ctx;
} PySSLContext;


typedef struct {
    // Data attached to a libuv stream.
    h2o_context_t    *context;
    h2o_globalconf_t *config;
    PyObject *callback;
    PyObject *ssl;
} h2py_data_t;


typedef struct {
    PyObject_HEAD
    size_t    server_cnt;
    uv_tcp_t *servers;
    h2py_data_t *data;
} H2PyServer;


typedef struct {
    PyObject_HEAD
    H2PyServer *server;
    h2o_req_t  *request;
    int started;
} H2PyRequest;


typedef struct {
    // An H2O handler with additional data.
    h2o_handler_t internal;
    H2PyServer *server;  // a `Server` instance
    PyObject *callback;  // a function to call with a request
    PyObject *ssl;       // and the SSL context to use
} h2py_handler_ext_t;


#endif // _EXTERNAL_H
