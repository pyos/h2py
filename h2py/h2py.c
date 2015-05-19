#include "h2py.h"


static PyObject * h2py_request_alloc(H2PyServer *parent, h2o_req_t *lowlevel)
{
    H2PyRequest *self = PyObject_New(H2PyRequest, &H2PyRequestType);

    if (self != NULL) {
        Py_INCREF(parent);
        self->server  = parent;
        self->request = lowlevel;
    }

    return (PyObject *) self;
}


static void h2py_request_dealloc(H2PyRequest *self)
{
    if (self->request && self->server->listeners.size != 0) {
        // Server still online, but no response was sent.
        h2o_send_error(self->request, 500, "Internal Server Error", "No response", 0);
    }

    Py_DECREF(self->server);
    Py_TYPE(self)->tp_free(self);
}


#define H2PY_ENSURE_REQUEST_USABLE(self) \
    do { if (self->request == NULL) \
        return PyErr_Format(PyExc_RuntimeError, "can't access request after response is sent"); } while(0)


static PyObject * h2py_request_respond(H2PyRequest *self, PyObject *args)
{
    H2PY_ENSURE_REQUEST_USABLE(self);

    if (self->server->listeners.size == 0) {
        return PyErr_Format(PyExc_ConnectionError, "server already closed");
    }

    PyObject *headers;
    h2o_iovec_t payload;

    if (!PyArg_ParseTuple(args, "iOy#", &self->request->res.status, &headers, &payload, &payload.len)) {
        return NULL;
    }

    if (!PyList_Check(headers)) {
        return PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
    }

    self->request->res.reason = "Or Something";
    self->request->res.headers = (h2o_headers_t) { };
    self->request->res.content_length = payload.len;

    Py_ssize_t i = 0;
    Py_ssize_t s = PyList_Size(headers);

    for (; i < s; ++i) {
        h2o_iovec_t key;
        h2o_iovec_t value;

        if (!PyArg_ParseTuple(PyList_GET_ITEM(headers, i), "s#s#", &key, &key.len, &value, &value.len)) {
            return PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
        }

        const h2o_token_t *token = h2o_lookup_token(key.base, key.len);

        if (token == NULL) {
            return PyErr_Format(PyExc_ValueError, "unknown header `%s`", key.base);
        }

        if (token != H2O_TOKEN_CONTENT_LENGTH) {
            // Content-Length is pretty much already known anyway.
            h2o_add_header(&self->request->pool, &self->request->res.headers, token, value.base, value.len);
        }
    }

    h2o_send_inline(self->request, payload.base, payload.len);
    // Once a response is sent, the stream is free for new requests. H2O can only
    // store a single request at a time, however, so this struct is now unsafe to use.
    self->request = NULL;
    Py_RETURN_NONE;
}


static PyObject * h2py_request_get_method(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return Py_BuildValue("s#", self->request->method.base, self->request->method.len);
}


static PyObject * h2py_request_get_path(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return Py_BuildValue("s#", self->request->path.base, self->request->path.len);
}


static PyObject * h2py_request_get_host(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return Py_BuildValue("s#", self->request->authority.base, self->request->authority.len);
}


static PyObject * h2py_request_get_upgrade(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return Py_BuildValue("s#", self->request->upgrade.base, self->request->upgrade.len);
}


static PyObject * h2py_request_get_version(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return Py_BuildValue("(ii)", self->request->version >> 8, self->request->version & 0xFF);
}


static PyObject * h2py_request_get_payload(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);
    return PyBytes_FromStringAndSize(self->request->entity.base, (ssize_t)self->request->entity.len);
}


static PyObject * h2py_request_get_headers(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);

    Py_ssize_t i;
    Py_ssize_t s = self->request->headers.size;
    PyObject *ret = PyList_New(s);
    h2o_header_t *src = self->request->headers.entries;

    if (ret != NULL) for (i = 0; i < s; ++i, ++src) {
        PyObject *pair = Py_BuildValue("(s#s#)", src->name->base, src->name->len,
                                                 src->value.base, src->value.len);

        if (pair == NULL) {
            Py_DECREF(ret);
            return NULL;
        }

        PyList_SET_ITEM(ret, i, pair);
    }

    return ret;
}


static PyObject * h2py_request_get_requests(H2PyRequest *self, void *closure)
{
    H2PY_ENSURE_REQUEST_USABLE(self);

    Py_ssize_t i;
    Py_ssize_t s = self->request->http2_push_paths.size;
    PyObject *ret = PyList_New(s);
    h2o_iovec_t *src = self->request->http2_push_paths.entries;

    if (ret != NULL) for (i = 0; i < s; ++i, ++src) {
        PyObject *path = Py_BuildValue("s#s#", src->base, src->len);

        if (path == NULL) {
            Py_DECREF(ret);
            return NULL;
        }

        PyList_SET_ITEM(ret, i, path);
    }

    return ret;
}


static PyGetSetDef H2PyRequestGetSetters[] = {
    { "method",   (getter) h2py_request_get_method,   NULL, NULL, NULL },
    { "path",     (getter) h2py_request_get_path,     NULL, NULL, NULL },
    { "host",     (getter) h2py_request_get_host,     NULL, NULL, NULL },
    { "upgrade",  (getter) h2py_request_get_upgrade,  NULL, NULL, NULL },
    { "version",  (getter) h2py_request_get_version,  NULL, NULL, NULL },
    { "headers",  (getter) h2py_request_get_headers,  NULL, NULL, NULL },
    { "payload",  (getter) h2py_request_get_payload,  NULL, NULL, NULL },
    { "requests", (getter) h2py_request_get_requests, NULL, NULL, NULL },
    { NULL }
};

static PyMethodDef H2PyRequestMethods[] = {
    { "respond", (PyCFunction) h2py_request_respond, METH_VARARGS, NULL },
    { NULL }
};


static PyTypeObject H2PyRequestType = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name      = "h2py.Request",
    .tp_basicsize = sizeof(H2PyRequest),
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_dealloc   = (destructor) h2py_request_dealloc,
    .tp_methods   = H2PyRequestMethods,
    .tp_getset    = H2PyRequestGetSetters,
};


static void on_connect(uv_stream_t *listener, int status)
{
    if (status != 0) return;

    uv_tcp_t *conn = PyMem_RawMalloc(sizeof(uv_tcp_t));
    uv_tcp_init(listener->loop, conn);

    if (uv_accept(listener, (uv_stream_t *) conn) != 0) {
        return uv_close((uv_handle_t *) conn, (uv_close_cb) PyMem_RawFree);
    }

    H2PyServer *server = (H2PyServer *) listener->data;
    h2o_socket_t *sock = h2o_uv_socket_create((uv_stream_t *) conn, NULL, 0, (uv_close_cb) PyMem_RawFree);

    if ((PyObject *) server->ssl != Py_None)
        h2o_accept_ssl(&server->context, server->config.hosts, sock, server->ssl->ctx);
    else
        h2o_http1_accept(&server->context, server->config.hosts, sock);
}


static int on_request(h2o_handler_t *self, h2o_req_t *req)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    H2PyServer *server = ((h2py_handler_t *) self)->server;
    PyObject *request = h2py_request_alloc(server, req);
    PyObject *result  = NULL;

    if (request != NULL) {
        result = PyObject_CallFunctionObjArgs(server->callback, request, NULL);
    }

    if (PyErr_Occurred()) {
        // We're not sending an error response here since a task might have already been spawned.
        // Nothing checks for an exception after this function returns, though; better print it.
        PyErr_Print();
        PyErr_Clear();
    }

    Py_XDECREF(result);
    Py_XDECREF(request);
    PyGILState_Release(gstate);
    return 0;
}


static PyObject * h2py_server_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *sockets, *callback;
    PyUVLoop *uvloop;
    PySSLContext *ssl;
    int backlog;

    if (!PyArg_ParseTuple(args, "OOOOi", &sockets, &callback, &uvloop, &ssl, &backlog)) {
        return NULL;
    }

    H2PyServer *self = (H2PyServer *) type->tp_alloc(type, 0);

    if (self == NULL) {
        return NULL;
    }

    // `PySSLContext` owns the `SSL_CTX` and will destroy it when GC'd.
    // So we have to store a reference to the whole object.
    Py_INCREF(ssl);
    Py_INCREF(callback);
    self->ssl               = ssl;
    self->callback          = callback;
    self->listeners.size    = 0;
    self->listeners.entries = NULL;

    // NOTE Python code should ensure that `ev` is a `pyuv.Loop`.
    //      And that `ssl` is either `None` or an `ssl.SSLContext`, for that matter.
    uv_loop_t *loop = uvloop->uv_loop;

    h2o_config_init(&self->config);
    // FIXME in theory, `h2o_config_register_host` and `h2o_create_handler`
    //       may return `NULL`.
    h2o_pathconf_t *path = &h2o_config_register_host(&self->config, h2o_iovec_init(H2O_STRLIT("default")), 65535)->fallback_path;
    h2py_handler_t *hndl = (h2py_handler_t *) h2o_create_handler(path, sizeof(h2py_handler_t));
    // This must be done *after* registering a host for some reason.
    h2o_context_init(&self->context, loop, &self->config);

    hndl->u.on_req = on_request;
    hndl->server   = self;

    Py_ssize_t i;
    Py_ssize_t sz = PyList_Size(sockets);
    uv_tcp_t *listeners = PyMem_RawMalloc(sizeof(uv_tcp_t) * sz);

    if (listeners == NULL) {
        PyErr_SetObject(PyExc_MemoryError, NULL);
        Py_DECREF(self);
        return NULL;
    }

    for (i = 0; i < sz; ++i) {
        uv_tcp_init(loop, listeners + i);
    }

    self->listeners.size    = sz;
    self->listeners.entries = listeners;

    for (i = 0; i < sz; ++i, ++listeners) {
        size_t fd = PyLong_AsSize_t(PyList_GET_ITEM(sockets, i));

        if (uv_tcp_open(listeners, fd)) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_IOError, "could not reopen %lu", fd);
        }

        listeners->data = (void *) self;

        if (uv_listen((uv_stream_t *) listeners, backlog, on_connect)) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_ConnectionError, "could not listen on %lu", fd);
        }
    }

    if ((PyObject *) ssl != Py_None) {
        #if H2O_USE_ALPN
            h2o_ssl_register_alpn_protocols(((PySSLContext *) ssl)->ctx, h2o_http2_alpn_protocols);
        #endif
        #if H2O_USE_NPN
            h2o_ssl_register_npn_protocols(((PySSLContext *) ssl)->ctx, h2o_http2_npn_protocols);
        #endif
    }

    return (PyObject *) self;
}


static void on_close(uv_handle_t * handle) {
    // This is now a pointer to the array of handles, not the server.
    // See below.
    PyMem_RawFree(handle->data);
}


static PyObject* h2py_server_close(H2PyServer *self) {
    uv_tcp_t *it  = self->listeners.entries;
    uv_tcp_t *end = self->listeners.entries + self->listeners.size;
    uv_close_cb on_close_ptr = on_close;

    for (; it != end; ++it) if (it->data && !uv_is_closing((uv_handle_t *) it))
    {
        // While the actual file descriptor is closed synchronously,
        // the handle itself is still held onto until the next iteration of the
        // event loop, so we have to wait before freeing memory.
        it->data = self->listeners.entries;
        // Unfortunately, this method may get called from the destructor, in which
        // case the `H2PyServer` instance will be deallocated when it returns,
        // so we need to replace it with a pointer to the array of handles.
        uv_close((uv_handle_t *) it, on_close_ptr);
        // Deallocating the array once is enough.
        on_close_ptr = NULL;
    }

    self->listeners.entries = NULL;
    self->listeners.size    = 0;
    Py_RETURN_NONE;
}


static void h2py_server_dealloc(H2PyServer *self)
{
    Py_DECREF(h2py_server_close(self));
    h2o_context_dispose(&self->context);
    h2o_config_dispose(&self->config);
    Py_XDECREF(self->callback);
    Py_XDECREF(self->ssl);
    Py_TYPE(self)->tp_free(self);
}


static PyMethodDef H2PyServerMethods[] = {
    { "close", (PyCFunction) h2py_server_close, METH_NOARGS, NULL },
    { NULL }
};


static PyTypeObject H2PyServerType = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name      = "h2py.Server",
    .tp_basicsize = sizeof(H2PyServer),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = (newfunc)    h2py_server_new,
    .tp_dealloc   = (destructor) h2py_server_dealloc,
    .tp_methods   = H2PyServerMethods,
};


static struct PyModuleDef h2pymodule = {
    PyModuleDef_HEAD_INIT, "h2py", NULL, -1, NULL
};


PyMODINIT_FUNC PyInit__unsafe(void)
{
    if (PyType_Ready(&H2PyServerType)  < 0) return NULL;
    if (PyType_Ready(&H2PyRequestType) < 0) return NULL;

    PyObject *m = PyModule_Create(&h2pymodule);

    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&H2PyServerType);
    Py_INCREF(&H2PyRequestType);
    PyModule_AddObject(m, "Server",  (PyObject *)&H2PyServerType);
    PyModule_AddObject(m, "Request", (PyObject *)&H2PyRequestType);
    return m;
}
