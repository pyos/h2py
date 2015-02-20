import asyncio
import inspect

from ssl import SSLContext
from socket import socket
from collections import Callable

from pyuv import Loop as _Loop
from ._unsafe import Server as Server, Request


try:
    from aiouv import EventLoop as _AIOLoop
except ImportError:
    _AIOLoop = None


class Server (Server):
    def __new__(cls, sockets, callback, loop=None, ssl=None, backlog=128):
        aioloop = None

        if _AIOLoop and isinstance(loop, _AIOLoop):
            aioloop, loop = loop, loop._loop

        if not isinstance(loop, _Loop):
            raise TypeError('{!r} is not a libuv loop'.format(loop))

        if not isinstance(callback, Callable):
            raise TypeError('{!r} is not callable'.format(callback))

        if asyncio.iscoroutinefunction(callback) or inspect.isgeneratorfunction(callback):
            if aioloop is None:
                raise TypeError('got a coroutine callback, but not an asyncio event loop')

            def callback(request, actual=callback):
                asyncio.async(actual(request, loop=aioloop), loop=aioloop)

        if ssl is not None and not isinstance(ssl, SSLContext):
            raise TypeError('{!r} is not an SSL context'.format(ssl))

        _s = []

        for sock in sockets:
            if isinstance(sock, int):
                _s.append(sock)
            else:
                sock.setblocking(False)
                _s.append(sock.detach())

        return super(Server, cls).__new__(cls, _s, callback, loop, ssl, backlog)
