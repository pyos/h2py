#include "h2py.h"

#define H2PY_RETURN(req) Py_INCREF(req); return req;
#define H2PY_STRING_IS(attr, name) if (PyUnicode_CompareWithASCIIString(attr, name) == 0)
#define H2PY_STRING(s) PyUnicode_DecodeUTF8((s).base, (ssize_t) (s).len, "surrogateescape")


static H2PyServer* h2py_server_close(H2PyServer *self) {
    size_t i;

    for (i = 0; i < self->server_cnt; ++i)
    {
        uv_close((uv_handle_t *) self->servers[i], (uv_close_cb) PyMem_RawFree);
    }

    self->servers = NULL;
    self->server_cnt = 0;
    H2PY_RETURN(self);
}


static void h2py_server_dealloc(H2PyServer *self)
{
    size_t i;

    for (i = 0; i < self->server_cnt; ++i)
    {
        self->servers[i]->data = NULL;
        uv_close((uv_handle_t *) self->servers[i], (uv_close_cb) PyMem_RawFree);
    }
 
    h2o_context_dispose(self->data->context);
    h2o_config_dispose(self->data->config);
    Py_DECREF(self->data->callback);
    Py_DECREF(self->data->ssl);
    PyMem_RawFree(self->data->config);
    PyMem_RawFree(self->data->context);
    PyMem_RawFree(self->data);
    self->data = NULL;
    self->servers = NULL;
    self->server_cnt = 0;
    Py_TYPE(self)->tp_free(self);
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
    0,                         /* tp_new */
};


static void h2py_request_dealloc(H2PyRequest *self)
{
    Py_XDECREF(self->server);
    self->server = NULL;
    Py_TYPE(self)->tp_free(self);
}


static H2PyRequest * h2py_request_respond(H2PyRequest *self, PyObject *args)
{
    if (self->server == NULL || self->server->server_cnt == 0) {
        PyErr_Format(PyExc_ConnectionError, "server already closed");
        return NULL;
    }

    if (self->started) {
        PyErr_Format(PyExc_ValueError, "called `respond` twice");
        return NULL;
    }

    PyObject *headers;
    char *data;
    int length;

    if (!PyArg_ParseTuple(args, "iOsy#", &self->request->res.status, &headers,
                                         &self->request->res.reason, &data, &length)) {
        return NULL;
    }

    if (!PyList_Check(headers)) {
        PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
        return NULL;
    }

    Py_ssize_t i = 0;
    Py_ssize_t s = PyList_Size(headers);

    for (; i < s; ++i) {
        PyObject *item = PyList_GET_ITEM(headers, i);

        if (!PyTuple_Check(item)) {
            PyErr_Format(PyExc_TypeError, "expected a list of (header, value) tuples");
            return NULL;
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
            PyErr_Format(PyExc_ValueError, "unsupported header %s", name);
            return NULL;
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
            Py_ssize_t i, k;
            Py_ssize_t s = (ssize_t) self->request->headers.size;
            h2o_header_t *src = self->request->headers.entries;
            PyObject *ret = PyList_New(s);

            if (ret == NULL) {
                return NULL;
            }

            for (i = 0; i < s; ++i) {
                PyObject *key = H2PY_STRING(src[i].name[0]);

                if (key == NULL) {
                    goto reset;
                }

                PyObject *val = H2PY_STRING(src[i].value);

                if (val == NULL) {
                    Py_DECREF(key);
                    goto reset;
                }

                PyObject *pair = PyTuple_Pack(2, key, val);

                if (pair == NULL) {
                    Py_DECREF(key);
                    Py_DECREF(val);
                    goto reset;
                }

                PyList_SET_ITEM(ret, i, pair);
            }

            return ret;

          reset:
            for (k = 0; k < i; ++k) {
                Py_DECREF(PyList_GET_ITEM(ret, k));
            }

            return NULL;
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

    //if (extra->ssl && extra->ssl != Py_None)
    //    // TODO SSL
    //else
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


static PyObject * h2py_server_from_addr(PyObject *self, PyObject *args)
{
    const char *addr;
    int port;
    PyObject *ev;
    PyObject *cb;
    PyObject *ssl = Py_None;
    int family  = AF_INET;
    int flags   = 0;
    int backlog = 128;
    int r;
    struct sockaddr *sockaddr;
    struct sockaddr_in  sockaddr4;
    struct sockaddr_in6 sockaddr6;

    if (!PyArg_ParseTuple(args, "siOO|Oiii", &addr, &port, &ev, &cb, &ssl, &family, &flags, &backlog))
        return NULL;

    if (!PyCallable_Check(cb)) {
        return PyErr_Format(PyExc_TypeError, "callback is not callable");
    }

    H2PyServer *ob = PyObject_New(H2PyServer, &H2PyServerType);

    if (ob == NULL) {
        return NULL;
    }

    uv_loop_t        *loop     = ((PyUV_Loop *)ev)->uv_loop;
    uv_tcp_t         *listener = PyMem_RawMalloc(sizeof(uv_tcp_t));
    h2py_data_t      *data     = PyMem_RawMalloc(sizeof(h2py_data_t));
    h2o_context_t    *context  = PyMem_RawMalloc(sizeof(h2o_context_t));
    h2o_globalconf_t *config   = PyMem_RawMalloc(sizeof(h2o_globalconf_t));

    ob->server_cnt = 1;
    ob->servers    = PyMem_RawMalloc(sizeof(void *));
    ob->servers[0] = listener;
    ob->data       = data;
    data->config   = config;
    data->context  = context;
    data->callback = cb;
    data->ssl      = ssl;
    listener->data = (void *) data;

    if ((r = uv_tcp_init(loop, listener)) != 0) {
        PyErr_Format(PyExc_ConnectionError, "could not create socket: %s", uv_strerror(r));
        goto error;
    }

    switch (family) {
        case AF_UNSPEC:
        case AF_INET:
            sockaddr = (struct sockaddr *)&sockaddr4;
            uv_ip4_addr(addr, port, &sockaddr4);
            break;

        case AF_INET6:
            sockaddr = (struct sockaddr *)&sockaddr6;
            uv_ip6_addr(addr, port, &sockaddr6);
            break;

        case AF_UNIX:
            // TODO

        default:
            PyErr_Format(PyExc_ValueError, "unsupported socket family %d", family);
            goto error;
    }

    if ((r = uv_tcp_bind(listener, sockaddr, flags)) != 0) {
        PyErr_Format(PyExc_ConnectionError, "could not bind: %s", uv_strerror(r));
        goto error;
    }

    if ((r = uv_listen((uv_stream_t *)listener, backlog, on_connect)) != 0) {
        PyErr_Format(PyExc_ConnectionError, "could not listen: %s", uv_strerror(r));
        goto error;
    }

    h2o_config_init(config);
    h2o_hostconf_t *host = h2o_config_register_host(config, "default");
    h2o_pathconf_t *path = h2o_config_register_path(host, "/");
    h2py_handler_ext_t *h = (h2py_handler_ext_t *) h2o_create_handler(path, sizeof(h2py_handler_ext_t));
    h->internal.on_req = on_request;
    h->callback = cb;
    h->server   = ob;
    h->ssl      = ssl;

    h2o_context_init(context, loop, config);

    Py_INCREF(cb);
    Py_INCREF(ssl);
    return (PyObject *) ob;

  error:
    PyMem_RawFree(data);
    PyMem_RawFree(config);
    PyMem_RawFree(context);
    PyMem_RawFree(ob->servers);
    uv_close((uv_handle_t *)listener, (uv_close_cb) PyMem_RawFree);
    Py_DECREF(ob);
    return NULL;
}


static PyObject * h2py_server_from_sock(PyObject *self, PyObject *args)
{
    return PyErr_Format(PyExc_NotImplementedError, "TODO");
}


static PyMethodDef H2PyMethods[] = {
    { "from_address", h2py_server_from_addr, METH_VARARGS, NULL },
    { "from_sockets", h2py_server_from_sock, METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};


static struct PyModuleDef h2pymodule = {
    PyModuleDef_HEAD_INIT, "h2py", NULL, -1, H2PyMethods
};


PyMODINIT_FUNC PyInit_h2py(void)
{
    if (PyType_Ready(&H2PyServerType) < 0) {
        return NULL;
    }

    if (PyType_Ready(&H2PyRequestType) < 0) {
        return NULL;
    }

    PyObject *m = PyModule_Create(&h2pymodule);

    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&H2PyServerType);
    Py_INCREF(&H2PyRequestType);
    PyModule_AddObject(m, "Server", (PyObject *)&H2PyServerType);
    PyModule_AddObject(m, "Request", (PyObject *)&H2PyRequestType);
    return m;
}
