#ifndef _H2PY_H_
#define _H2PY_H_
#define PY_SSIZE_T_CLEAN  // use Py_ssize_t, not int

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
} PyUVLoop;


typedef struct {
    // Same story with PySSLContext objects.
    // https://hg.python.org/cpython/file/70a55b2dee71/Modules/_ssl.c#l184
    PyObject_HEAD
    SSL_CTX *ctx;
} PySSLContext;


typedef struct {
    PyObject_HEAD
    H2O_VECTOR(uv_tcp_t) listeners;
    h2o_context_t    context;
    h2o_globalconf_t config;
    PySSLContext *ssl;
    PyObject *callback;
} H2PyServer;


typedef struct {
    PyObject_HEAD
    H2PyServer *server;
    h2o_req_t  *request;
} H2PyRequest;


typedef struct {
    h2o_handler_t u;
    H2PyServer *server;
} h2py_handler_t;


static PyTypeObject H2PyRequestType;
static PyTypeObject H2PyServerType;

#endif // _H2PY_H_
