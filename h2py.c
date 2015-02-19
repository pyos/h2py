#include "h2py.h"

#define H2PY_RETURN(req) Py_INCREF(req); return (PyObject *)(req);
#define H2PY_STRING_IS(attr, name) if (PyUnicode_CompareWithASCIIString(attr, name) == 0)
#define H2PY_STRING(s) PyUnicode_DecodeUTF8((s).base, (ssize_t) (s).len, "surrogateescape")


static PyObject * SSL_CONTEXT_TYPE;
static PyObject * PYUV_LOOP_TYPE;


static void h2py_request_dealloc(H2PyRequest *self)
{
    Py_XDECREF(self->server);
    self->server = NULL;
    Py_TYPE(self)->tp_free(self);
}


static PyObject * h2py_request_respond(H2PyRequest *self, PyObject *args)
{
    if (self->started) {
        return PyErr_Format(PyExc_ValueError, "called `respond` twice");
    }

    if (self->server == NULL || self->server->server_cnt == 0) {
        return PyErr_Format(PyExc_ConnectionError, "server already closed");
    }

    PyObject *headers;
    char *data;
    int length;

    if (!PyArg_ParseTuple(args, "iOsy#", &self->request->res.status, &headers,
                                         &self->request->res.reason, &data, &length)) {
        return NULL;
    }

    if (!PyList_Check(headers)) {
        return PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
    }

    Py_ssize_t i = 0;
    Py_ssize_t s = PyList_Size(headers);

    for (; i < s; ++i) {
        PyObject *item = PyList_GET_ITEM(headers, i);

        if (!PyTuple_Check(item)) {
            return PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
        }

        char *name;
        char *value;
        int ln_name;
        int ln_value;

        if (!PyArg_ParseTuple(item, "s#s#", &name, &ln_name, &value, &ln_value)) {
            return NULL;
        }

        const h2o_token_t *token = h2o_lookup_token(name, (size_t) ln_name);

        if (token == NULL) {
            return PyErr_Format(PyExc_ValueError, "unsupported header %s", name);
        }

        if (token == H2O_TOKEN_CONTENT_LENGTH) {
            self->request->res.content_length = atoi(value);
        } else {
            h2o_add_header(&self->request->pool, &self->request->res.headers, token, value, (size_t) ln_value);
        }
    }

    self->started = 1;
    h2o_send_inline(self->request, data, (size_t) length);
    H2PY_RETURN(self);
}


static PyObject * h2py_request_getattro(H2PyRequest *self, PyObject *name)
{
    if (PyUnicode_Check(name)) {
        H2PY_STRING_IS(name, "method") {
            return H2PY_STRING(self->request->method);
        }

        H2PY_STRING_IS(name, "path") {
            return H2PY_STRING(self->request->path);
        }

        H2PY_STRING_IS(name, "host") {
            return H2PY_STRING(self->request->authority);
        }

        H2PY_STRING_IS(name, "upgrade") {
            if (self->request->upgrade.base == NULL) {
                Py_RETURN_NONE;
            } else {
                return H2PY_STRING(self->request->upgrade);
            }
        }

        H2PY_STRING_IS(name, "version") {
            int major = self->request->version >> 8;
            int minor = self->request->version & 0xFF;
            return Py_BuildValue("(ii)", major, minor);
        }

        H2PY_STRING_IS(name, "payload") {
            if (self->request->entity.base == NULL) {
                Py_RETURN_NONE;
            } else {
                return PyBytes_FromStringAndSize(self->request->entity.base,
                                       (ssize_t) self->request->entity.len);
            }
        }

        H2PY_STRING_IS(name, "headers") {
            Py_ssize_t i;
            Py_ssize_t s = (ssize_t) self->request->headers.size;
            h2o_header_t *src = self->request->headers.entries;
            PyObject *ret = PyList_New(s);

            if (ret == NULL) {
                return NULL;
            }

            for (i = 0; i < s; ++i) {
                PyObject *key = H2PY_STRING(src[i].name[0]);
                PyObject *val = H2PY_STRING(src[i].value);
                PyObject *pair = PyTuple_Pack(2, key, val);

                Py_XDECREF(key);
                Py_XDECREF(val);
                if (pair == NULL) {
                    while (i--) {
                        Py_DECREF(PyList_GET_ITEM(ret, i));
                    }

                    return NULL;
                }

                PyList_SET_ITEM(ret, i, pair);
            }

            return ret;
        }
    }

    return PyObject_GenericGetAttr((PyObject *) self, name);
}


static PyMethodDef H2PyRequestMethods[] = {
    { "respond", (PyCFunction) h2py_request_respond, METH_VARARGS, NULL },
    { NULL }
};


static PyTypeObject H2PyRequestType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "h2py.Request",            /* tp_name */
    sizeof(H2PyRequest),       /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor) h2py_request_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    (getattrofunc) h2py_request_getattro, /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    NULL,                      /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    H2PyRequestMethods,        /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
};


static PyObject* h2py_server_close(H2PyServer *self) {
    size_t i;

    for (i = 0; i < self->server_cnt; ++i)
    {
        uv_close((uv_handle_t *) (self->servers + i), (uv_close_cb) PyMem_RawFree);
    }

    self->servers = NULL;
    self->server_cnt = 0;
    H2PY_RETURN(self);
}


static void h2py_server_dealloc(H2PyServer *self)
{
    size_t i;

    for (i = 0; i < self->server_cnt; ++i) if (self->servers[i].data)
    {
        self->servers[i].data = NULL;
        uv_close((uv_handle_t *) (self->servers + i), (uv_close_cb) PyMem_RawFree);
    }

    if (self->data) {
        h2o_context_dispose(self->data->context);
        h2o_config_dispose(self->data->config);
        Py_DECREF(self->data->callback);
        Py_DECREF(self->data->ssl);
        PyMem_RawFree(self->data->config);
        PyMem_RawFree(self->data->context);
        PyMem_RawFree(self->data);
    }

    self->data = NULL;
    self->servers = NULL;
    self->server_cnt = 0;
    Py_TYPE(self)->tp_free(self);
}


static void on_connect(uv_stream_t *server, int status)
{
    if (status != 0) return;

    uv_tcp_t *conn = PyMem_RawMalloc(sizeof(uv_tcp_t));
    uv_tcp_init(server->loop, conn);

    if (uv_accept(server, (uv_stream_t *)conn) != 0) {
        uv_close((uv_handle_t *)conn, (uv_close_cb)PyMem_RawFree);
        return;
    }

    h2py_data_t *extra = (h2py_data_t *) server->data;
    h2o_context_t *ctx = extra->context;
    h2o_socket_t *sock = h2o_uv_socket_create((uv_stream_t *)conn, NULL, 0, (uv_close_cb)PyMem_RawFree);

    if (extra->ssl && extra->ssl != Py_None)
        h2o_accept_ssl(ctx, ctx->globalconf->hosts, sock, ((PySSLContext *) extra->ssl)->ctx);
    else
        h2o_http1_accept(ctx, ctx->globalconf->hosts, sock);
}


static int on_request(h2o_handler_t *self_, h2o_req_t *req)
{
    PyGILState_STATE gstate = PyGILState_Ensure();

    h2py_handler_ext_t *self = (h2py_handler_ext_t *)self_;
    H2PyRequest *reqobj = PyObject_New(H2PyRequest, &H2PyRequestType);

    if (reqobj == NULL) {
        PyErr_Print();
        PyErr_Clear();
        PyGILState_Release(gstate);
        return -1;
    }

    Py_INCREF(self->server);
    reqobj->server  = self->server;
    reqobj->request = req;
    reqobj->started = 0;

    PyObject *result = PyObject_CallFunctionObjArgs(self->callback, reqobj, NULL);

    if (result == NULL) {
        PyErr_Print();
        PyErr_Clear();
        Py_DECREF(reqobj);
        PyGILState_Release(gstate);
        return reqobj->started - 1;
    }

    Py_DECREF(result);
    Py_DECREF(reqobj);
    PyGILState_Release(gstate);
    return 0;
}


static PyObject * h2py_server_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *sockets;
    PyObject *ev;
    PyObject *cb;
    PyObject *ssl = Py_None;
    Py_ssize_t i;
    Py_ssize_t sz;
    int backlog = 128;
    int r;

    if (!PyArg_ParseTuple(args, "OOO|Oi", &sockets, &ev, &cb, &ssl, &backlog)) {
        return NULL;
    }

    if (!PyList_Check(sockets)) {
        return PyErr_Format(PyExc_TypeError, "expected a list of file descriptors");
    }

    if (!PyCallable_Check(cb)) {
        return PyErr_Format(PyExc_TypeError, "callback is not callable");
    }

    if (!PyObject_IsInstance(ev, PYUV_LOOP_TYPE)) {
        return PyErr_Format(PyExc_TypeError, "expected `pyuv.Loop`, got `%s`",
            Py_TYPE(ev)->tp_name);
    }

    if (ssl != Py_None && !PyObject_IsInstance(ssl, SSL_CONTEXT_TYPE)) {
        return PyErr_Format(PyExc_TypeError, "expected `ssl.SSLContext` or None, got `%s`",
            Py_TYPE(ssl)->tp_name);
    }

    H2PyServer *self = (H2PyServer *) type->tp_alloc(type, 0);

    if (self == NULL) {
        return NULL;
    }

    self->server_cnt = 0;
    self->servers    = NULL;
    self->data       = NULL;

    sz = PyList_Size(sockets);
    uv_tcp_t         *servers  = PyMem_RawMalloc(sizeof(uv_tcp_t) * sz);
    h2py_data_t      *data     = PyMem_RawMalloc(sizeof(h2py_data_t));
    h2o_context_t    *context  = PyMem_RawMalloc(sizeof(h2o_context_t));
    h2o_globalconf_t *config   = PyMem_RawMalloc(sizeof(h2o_globalconf_t));

    if (servers == NULL || data == NULL || context == NULL || config == NULL) {
        PyMem_RawFree(servers);
        PyMem_RawFree(context);
        PyMem_RawFree(config);
        PyMem_RawFree(data);
        PyErr_SetObject(PyExc_MemoryError, NULL);
        Py_DECREF(self);
        return NULL;
    }

    Py_INCREF(cb);
    Py_INCREF(ssl);

    self->data = data;
    self->data->config   = config;
    self->data->context  = context;
    self->data->callback = cb;
    self->data->ssl      = ssl;
    self->server_cnt = sz;
    self->servers    = servers;

    uv_loop_t *loop = ((PyUV_Loop *)ev)->uv_loop;

    h2o_config_init(config);
    h2o_hostconf_t *host = h2o_config_register_host(config, "default");
    h2o_pathconf_t *path = h2o_config_register_path(host, "/");
    h2py_handler_ext_t *h = (h2py_handler_ext_t *) h2o_create_handler(path, sizeof(h2py_handler_ext_t));
    h2o_context_init(context, loop, config);

    h->internal.on_req = on_request;
    h->callback = cb;
    h->server   = self;
    h->ssl      = ssl;

    for (i = 0; i < sz; ++i) {
        self->servers[i].data = NULL;
    }

    for (i = 0; i < sz; ++i) {
        PyObject *socket = PyList_GET_ITEM(sockets, i);
        uv_tcp_t *listener = self->servers + i;
        listener->data = (void *) self->data;

        if (!PyLong_Check(socket)) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_TypeError, "file descriptors must be integers");
        }

        if ((r = uv_tcp_init(loop, listener)) != 0) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_ConnectionError, "could not create socket: %s", uv_strerror(r));
        }

        size_t fd = PyLong_AsSize_t(socket);

        if ((r = uv_tcp_open(listener, fd)) != 0) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_ConnectionError, "could not reopen fd %lu: %s", fd, uv_strerror(r));
        }

        if ((r = uv_listen((uv_stream_t *)listener, backlog, on_connect)) != 0) {
            Py_DECREF(self);
            return PyErr_Format(PyExc_ConnectionError, "could not listen on %lu: %s", fd, uv_strerror(r));
        }
    }

    return (PyObject *) self;
}


static PyMethodDef H2PyServerMethods[] = {
    { "close", (PyCFunction) h2py_server_close, METH_NOARGS, NULL },
    { NULL }
};


static PyTypeObject H2PyServerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "h2py.Server",             /* tp_name */
    sizeof(H2PyServer),        /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor) h2py_server_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    NULL,                      /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    H2PyServerMethods,         /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    h2py_server_new,           /* tp_new */
};


static struct PyModuleDef h2pymodule = {
    PyModuleDef_HEAD_INIT, "h2py", NULL, -1, NULL
};


PyMODINIT_FUNC PyInit_h2py(void)
{
    if (PyType_Ready(&H2PyServerType)  < 0) return NULL;
    if (PyType_Ready(&H2PyRequestType) < 0) return NULL;

    PyObject *m    = NULL;
    PyObject *ssl  = NULL;
    PyObject *pyuv = NULL;

    if ((m    = PyModule_Create(&h2pymodule))  == NULL) goto error;
    if ((ssl  = PyImport_ImportModule("ssl"))  == NULL) goto error;
    if ((pyuv = PyImport_ImportModule("pyuv")) == NULL) goto error;
    if ((SSL_CONTEXT_TYPE = PyObject_GetAttrString(ssl,  "SSLContext")) == NULL) goto error;
    if ((PYUV_LOOP_TYPE   = PyObject_GetAttrString(pyuv, "Loop"))       == NULL) goto error;
    Py_XDECREF(ssl);
    Py_XDECREF(pyuv);

    Py_INCREF(&H2PyServerType);
    Py_INCREF(&H2PyRequestType);
    PyModule_AddObject(m, "Server",  (PyObject *)&H2PyServerType);
    PyModule_AddObject(m, "Request", (PyObject *)&H2PyRequestType);
    // This will ensure correct reference counts.
    PyModule_AddObject(m, "SSLContext", SSL_CONTEXT_TYPE);
    PyModule_AddObject(m, "EventLoop",  PYUV_LOOP_TYPE);
    return m;

  error:
    Py_XDECREF(m);
    Py_XDECREF(ssl);
    Py_XDECREF(pyuv);
    return NULL;
}
