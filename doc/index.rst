.. Termpaint documentation master file

Termpaint
=========

Termpaint is a low level terminal interface library for character-cell terminals
in the tradition of VT1xx (like xterm, etc).

It's designed to be portable and flexible to integrate. It covers event handling and rendering.

Features
--------

* works with and without an central event loop
* robust input handling, unknown key events are gracefully filtered out
* truecolor, soft line breaks, explicit control of trailing whitespace
* double and curly underlines, custom underline colors
* flexible support for program supplied additional formatting like hyperlinks
* block, underline and bar cursor shape
* simple grid of character cell design, support for wide characters
* does not depend on correctly set $TERM or terminfo database
* mouse event handling
* tagged paste
* mostly utf-8 based, string width routines also handle utf-16 and utf-32
* offscreen surfaces/layers
* interface with opaque structures designed for ABI stability (but breaking changes are still happening)
* possible to use where allocation failure needs to be handled gracefully
* does not use global variables where possible, can handle multiple terminals in one process
* input parsing subset usable standalone
* permissively licensed: Boost Software License 1.0

Does not contain:

* ready made user interface elements (form, menu or similar)
* window or panel abstractions
* support for non utf-8 capable terminals

Termpaint is meant as a basic building block to build more specific libraries upon. There are a lot
of different higher layer styles, so it's cleaner to have separate libraries for this.

.. TODO link libraries when something is released in this space

Minimal example
---------------

A "hello world", using the internal default operating system integration and opinionated default setup.

See :doc:`getting-started` for full source.

.. literalinclude:: getting-started.c
    :caption: main code
    :start-after: // snippet-main-start
    :end-before: // snippet-main-end

.. literalinclude:: getting-started.c
    :caption: event callback
    :start-after: // snippet-callback-start
    :end-before: // snippet-callback-end

Support
-------

It's known to work on
 * xterm
 * vte
 * rxvt-unicode
 * mintty
 * iTerm2
 * microsoft terminal
 * putty
 * konsole
 * linux
 * freebsd
 * and more.

The core library (but not the OS integration and the meson build system) only depend on C11 (plus a few common string
functions like strdup).


.. toctree::
   :maxdepth: 2
   :caption: Contents:

   getting-started
   terminal
   integration
   surface
   attributes
   measuring
   events
   details
   termpaint_input
   termpaintx
   image

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
