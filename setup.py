#!/usr/bin/env python3
from distutils.core import setup, Extension

setup(
    name='h2py',
    version='1.0.0',
    description='Python bindings for libh2o server',
    author='pyos',
    author_email='pyos100500@gmail.com',
    url='https://github.com/pyos/h2py',
    ext_modules=[
        Extension('h2py', ['h2py.c'], libraries=['h2o', 'ssl', 'uv'])
    ]
)
