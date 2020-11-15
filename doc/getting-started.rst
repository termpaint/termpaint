Getting started
===============

Termpaint is a low level terminal interface library. The output model is a grid
of character cells with :ref:`attributes<sec_attributes>` specifying colors and
style (like bold or underline).

Input from the terminal is passed into the library which then calls a
user-provided event callback with e.g. character or key press events.

The core of termpaint does not do any operating system or environment specific
I/O. In the general case the application provides an integration pointer and
callbacks for all terminal I/O. This integration can connect to an existing
event loop or just wrap blocking terminal I/O.

For applications that deal with any kind of asynchronous events, apart from the
terminal, it's recommended to use a custom integration or an external integration
into an general event loop. But for simple applications that only deal with
synchronous data sources termpaint ships with a very simplistic sample
integration in a add-on called :doc:`termpaintx<termpaintx>`. This integration helps to get
started quickly for simple applications. Let's start with a "Hello World" style
application.

Hello world
-----------

First you need to include the pkg-config dependency named ``termpaint`` into
your application's build. Next include the needed headers into your source:

.. literalinclude:: getting-started.c
    :caption: includes
    :start-after: // snippet-header-start
    :end-before:  // snippet-header-end

The most code is for the main function:

.. literalinclude:: getting-started.c
    :caption: main code
    :linenos:
    :dedent: 4
    :start-after: // snippet-main-start
    :end-before: // snippet-main-end

This code starts with a few variables needed later. ``integration`` stores a
pointer to the :c:type:`integration object<termpaint_integration>` needed
after initialization to run keyboard
input handling. ``terminal`` is used for interacting with the terminal as a
whole and ``surface`` for interaction with the character cell grid. Finally
``quit`` is used for communication between the event handler (see below) and
the loop waiting in the main function.

The first call uses :c:func:`termpaintx_full_integration_setup_terminal_fullscreen()`
to setup termpaint for full screen usage with the simple termpaintx
integration. Its first arguement is a string with
:ref:`options for termpaint<termpaint-fullscreen-options>` and
:ref:`options for termpaintx<termpaintx-options>`, followed by
the event callback and it's data pointer. The last parameter is an out-parameter
and together with the return value they produce the pointers to the integration
object and the terminal object.

The following line extracts the pointer to the primary surface of the terminal
object. Surfaces in termpaint are objects that represent a character cell grid
that have functions to allow writing text.

The next line clears the whole primary surface (which later is transferred to
the terminal) with the given colors. It takes two colors because some terminals
behave oddly if cleared space has the same foreground and background color set [#defcol]_.
When two colors are specified in termpaint the order is always foreground color
and then background color. In this example the :ref:`default colors<default-colors>`
are used. These are special colors defined by the terminal. Additionally
:ref:`named<named-colors>`, :ref:`indexed<indexed-colors>`
and :ref:`rgb<rgb-colors>` colors are available.

:c:func:`termpaint_surface_write_with_colors` places text on the surface. It takes
the position for the text to start, the text itself (in utf-8) and the colors.
In termpaint coordinates are always 0-based and start in the upper-left corner.

The next line actually displays the content of the primary surface by calling
:c:func:`termpaint_terminal_flush()`.

The code that follows waits for the user to press ``q`` or the escape
key. :c:func:`termpaintx_full_integration_do_iteration` waits for input from
the terminal and lets termpaint process the data into events.
For deciding if the input matches this example uses the event callback:

.. literalinclude:: getting-started.c
    :caption: event callback
    :linenos:
    :start-after: // snippet-callback-start
    :end-before: // snippet-callback-end

This is a very simple callback only suitable for the example. Termpaint uses
:doc:`events of various types<events>`. But the most important events are key
and character events. :ref:`Character events <character event>` (``TERMPAINT_EV_CHAR``)
are emitted for printable characters like letters. :ref:`Key events <key event>`
(``TERMPAINT_EV_KEY``) are for other keys like function keys, enter or space.
Both additionally contain a bitflag that describes modifiers like alt and ctrl
that have been held down while pressing the key. The strings are not
nul-terminated. For key events the ``atom`` field can either be used as a string
or directly compared to the return of functions like ``termpaint_input_escape()``.
See :doc:`keys` for a complete list of keys.

Finally before termination the application :c:func:`termpaint_terminal_free_with_restore()`
should be called to restore the terminal to it's normal state.

See :download:`getting-started.c` for the whole source of this example.

Input handling strategies
-------------------------

Synchronous usage
.................

Termpaint uses an event callback based design for handling input events. This fits
well into designs which use a central event processing loop but out of the box it
does not fit well for code that wants to synchronously wait for input in many
places while processing, e.g. a usage styles where a menu or such is implemented by
a function that only returns after the user has completed interacting with it.

For such synchronous usage it's recommended to use an callback that just adds
events to a application specific queue of events. And then implement a function
that passes input from the terminal to termpaint until a new event appears.

In this way the application can also filter out events, it does not need, in these
functions. As termpaint tries to avoid allocations in steady state operation,
the event callback has to copy all needed data out of the original event to a
safe place.

First we need some data structures and variables to save the events:

.. literalinclude:: sync-event-handling.c
    :linenos:
    :start-after: // snippet-type-start
    :end-before: // snippet-type-end

Here the key or character is represented by a freshly allocated nul-terminated string.

The events are copied into this structure in the event handler:

.. literalinclude:: sync-event-handling.c
    :linenos:
    :start-after: // snippet-callback-start
    :end-before: // snippet-callback-end

In this case only key or character events are translated, all other events are
filtered out. For the events the type, string and modifiers are copied and the
new event is added to a linked list.

Next we need a function for the application to call to wait for the next event:

.. literalinclude:: sync-event-handling.c
    :linenos:
    :start-after: // snippet-wait-start
    :end-before: // snippet-wait-end

The ``key_wait`` function either returns an already queued event or if
no event is queued it calls :c:func:`termpaintx_full_integration_do_iteration`
to wait for terminal data and process it. As soon as enough data is read and
processed to give one or more suitable events it stops waiting for data and
returns the first event. To easy usage this implementation internally handles
freeing the last returned event in the next ``key_wait`` call.

Now the application can synchronously wait for events like this:

.. literalinclude:: sync-event-handling.c
    :linenos:
    :dedent: 4
    :start-after: // snippet-main-start
    :end-before: // snippet-main-end

Remember to flush the primary surface to make sure that the user can see the
most recent content while the application waits for input. Or maybe even
move the flush into the wait function, if so desired.

Given such an synchronous input function, parts of the user interface can be
written as stand-alone functions like this:

.. literalinclude:: sync-event-handling.c
    :linenos:
    :start-after: // snippet-menu-start
    :end-before: // snippet-menu-end

See :download:`sync-event-handling.c` for the whole source of this example.

Callback usage
..............

Using callbacks directly needs some kind of event dispatching in the application.
There are many ways this could be done, ranging in granularity from having one
event processing function for the whole application over a function per "page"
or having a fine grained "widget" structure with elaborate event routing rules.

A simple way to do event routing is to have global variables for an event handler
and a void pointer for it's data and switch those around while the user moves
through the application. A very rough version of something like this could look
like this:

Some global variables for drawing and event handling:

.. literalinclude:: callback-event-handling.c
    :linenos:
    :start-after: // snippet-globals-start
    :end-before: // snippet-globals-end

Instead of passing the terminal and surface object around the whole application
as parameters, having them as globals simplifies the code a lot.

The global ``current_callback`` and it's ``current_data`` allows switching which
function will get events to process while the user moves through the application.

.. literalinclude:: callback-event-handling.c
    :linenos:
    :start-after: // snippet-callback-start
    :end-before: // snippet-callback-end

The callback passed to the terminal object just passes the event on to the
currently active event processing function.

The following is a simple confirm dialog. It can be started with a pointer to
a bool where it stores it's result. When started it replaces the currently
active event handler with it's own. When it is finished it saves the result,
restores the previous handler and calls it without an event to indicate that
the result is ready.

First the confirm dialog needs a place to store it's data:

.. literalinclude:: callback-event-handling.c
    :linenos:
    :start-after: // snippet-quit-data-start
    :end-before: // snippet-quit-data-end

Than the start function can save the old event handler and the result pointer
and draw the initial user interface:

.. literalinclude:: callback-event-handling.c
    :linenos:
    :start-after: // snippet-quit-ctor-start
    :end-before: // snippet-quit-ctor-end

Finally the event handling function reacts to user input and when finished
saves the result, restores the event handler and calls it:

.. literalinclude:: callback-event-handling.c
    :linenos:
    :start-after: // snippet-quit-callback-start
    :end-before: // snippet-quit-callback-end

When using callbacks as primary means of composing the application logic,
the main function just needs to wait for events in one central place.

Thus the central loop can simply be:

.. literalinclude:: callback-event-handling.c
    :linenos:
    :dedent: 4
    :start-after: // snippet-main-start
    :end-before: // snippet-main-end

See :download:`callback-event-handling.c` for the whole source of this example.

Event loop usage
................

Existing event loops differ in their API in various details, but the general
approach is fairly similar between common event loops.

Currently there are no ready made integrations into event loops available.

The general steps are:

* Setup the operating system terminal I/O interface for unbuffered input and
  output processing

  On \*nix like operation systems this generally means saving the current
  settings via ``tcgetattr`` and configuring the needed new settings. Perhaps
  using :c:func:`termpaintx_fd_set_termios`.

* Bridge input from the terminal file descriptor to termpaint

  After registering an input available notifier a typical implemantation would
  be::

    gsize amount;
    g_io_channel_read_chars(channel, buf, sizeof(buf) - 1, &amount, NULL);

    termpaint_terminal_add_input_data(terminal, buf, amount);
* Pass output by termpaint to the operation system terminal interface

  For best performance the output from termpaint should be buffered and send in
  reasonable blocks to the terminal.

  The integration will receive output date via it's ``write`` callback and needs
  to flush it's output buffer when the ``flush`` callback is called.

For best performance the integration should additionally use
:c:func:`termpaint_integration_set_request_callback` to enable a mechanism for
a delayed callback used in cases where terminal input can not be readily parsed
without knowing if additional data is on it's way from the terminal to the
application.

.. rubric:: Footnotes

.. [#defcol] TERMPAINT_DEFAULT_COLOR is special as it is replaced by the
   terminals with globally set colors. So using TERMPAINT_DEFAULT_COLOR as
   foreground and background doesn't actually count as using the same color.
