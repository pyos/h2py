## h2py

Python bindings for [libh2o](https://github.com/h2o/h2o).

### Requirements

  * Python 3.4+;
  * [libh2o](https://github.com/h2o/h2o), obviously;
  * [pyuv](https://github.com/saghul/pyuv);
  * [aiouv](https://github.com/saghul/aiouv).

### Usage

```python
import socket

import h2py
import aiouv

# h2o only supports two kinds of event loops, libuv and its own one.
# Of these two, only libuv has asyncio bindings, so...
loop = aiouv.EventLoop()
loop.add_signal_handler(2, lambda: exit())

# Sockets need to be created manually.
sock = socket.socket(socket.AF_INET)
sock.bind(('', 8000))

def onrequest(req, loop=None):
    # Request objects have the following attributes:
    #   * method  :: str
    #   * path    :: str
    #   * host    :: str
    #   * upgrade :: str or None
    #   * version :: (int, int) -- (major, minor)
    #   * payload :: bytes
    #   * headers :: [(str, str)]
    req.respond(200, [('content-type', 'text/plain')], b'Success!\n')

#  * `sockets`: a list of sockets, opened in non-blocking mode and bound
#    to an interface. Either socket objects or `int` file descriptors are OK.
#  * `callback`: a function to call with each request.
#    If the provided event loop was an `aiouv.EventLoop`, this can be a coroutine.
#    It must also accept a keyword argument named `loop`, though.
#  * `loop`: a `pyuv` or `aiouv` event loop. If no provided, the default asyncio loop
#    is used (which, hopefully, is an `aiouv` loop. Otherwise, `TypeError` is raised.)
#  * `ssl`: either `None` for HTTP or an `ssl.SSLContext` for HTTPS.
#  * `backlog`: see `socket.accept`. Defaults to 128.
#
srv = h2py.Server([sock], onrequest, loop)

try:
    loop.run_forever()
finally:
    srv.close()
```
