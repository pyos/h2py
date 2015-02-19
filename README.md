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

# Sockets need to be created manually.
sock = socket.socket(socket.AF_INET)
sock.bind(('', 8000))
sock.setblocking(False)
fd = sock.detach()  # We won't need the object, only the descriptor.

def onrequest(req):
    # Request objects have the following attributes:
    #   * method  :: str
    #   * path    :: str
    #   * host    :: str
    #   * upgrade :: str or None
    #   * version :: (int, int) -- (major, minor)
    #   * payload :: bytes
    #   * headers :: [(str, str)]
    req.respond(200, [('content-length', '9')], 'OK', b'Success!\n')

# First argument: a list of sockets, opened in non-blocking mode and bound
#   to an interface. ("sockets" means "file descriptors", not "objects".)
# Second argument: a pyuv (*not* aiouv!) event loop.
# Third argument: a function to call with each request. A function, not a coroutine!
#   If you need to do asynchronous operations, spawn an `asyncio.Task` manually.
#   This is preferable, in fact, as exceptions raised by the callback do not pass
#   through normal exception machinery (they simply get dumped to stderr.)
# Fourth argument: an SSL context. Or `None`, if you don't want HTTPS.
# Fifth argument: backlog size, defaults to 128.
srv = h2py.Server([fd], loop._loop, onrequest, None, 128)

try:
    loop.run_forever()
finally:
    # Stop serving on these sockets. Note that this is technically not guaranteed
    # to happen until the next iteration of the event loop.
    srv.close()
```
