Integration
===========

.. c:type:: termpaint_integration

The core termpaint library does not come with integration into the operating system's input/output channels. It's designed
to be integrated into synchronous or asynchronous program designs by a pluggable integration abstraction.

There are auxiliary functions in the termpaintx_* namespace that have some common code that can be used for integrations.
But this code is fairly limited, so if more capabilities are needed feel free to copy this code into your project.

To use the simple premade integration see the :doc:`termpaintx addon<termpaintx>`.

A user supplied integration consists of 3 major parts: terminal to termpaint communication, termpaint to terminal
communication and terminal interface setup.

Termpaint uses a semi-opaque struct which contains callbacks for calling the application provided integration code when
needed. The :c:type:`termpaint_integration` structure can be used as part of a struct in the application provided
integration code. It must be initialized by calling :c:func:`termpaint_integration_init()` with the mandatory
callbacks. Additional optional callbacks can then be set with additional functions.

When the integration is no longer needed the allocated resources have
to be freed by calling :c:func:`termpaint_integration_deinit`, possibly from the integrations ``free`` callback.

The integration is connected to a terminal object by passing a pointer to its :c:type:`termpaint_integration` struct to
:c:func:`termpaint_terminal_new` when creating the terminal object.

Input bytes from the terminal to termpaint need to be passed to :c:func:`termpaint_terminal_add_input_data`. If enough
bytes have accumulated to identify a input sequence termpaint will call the event callback set by the application using
:c:func:`termpaint_terminal_set_event_cb` with the interpreted :doc:`event <events>`. The integration needs to take
care, that :c:func:`termpaint_terminal_add_input_data` is not called recursively from the callbacks set on the terminal.

Some platforms have kernel level terminal processing that needs to be configured for termpaint to work. On \*nix-like
platforms the kernel tty interface can be setup with :c:func:`termpaintx_fd_set_termios`. For details see the
implementation of that function. In general the terminal interface should be set to disable all kernel interpretation
and transformation features. If keyboard signal handling (ctrl-c, etc) is needed it can be left enabled. But in that
case the terminal object needs to be configured with ``+kbdsig`` to avoid switching keyboard input into advanced modes
that would be incompatible with kernel signal generation.

In addition to the kernel interface the terminal needs to be setup using configuration sequences. For this
:c:func:`termpaint_terminal_setup_fullscreen` needs to be called with the size of the terminal.



All callbacks of the integration receive a pointer to the integration struct as the first parameter. If the integration
just uses global variables the pointer can be ignored. If the integration itself uses a struct with data members the
recommended setup is to begin the custom struct with :c:type:`termpaint_integration`::

  struct custom_integration {
      termpaint_integration base;
      // additional members go here.
  }

Then the callbacks can just cast their first argument to a pointer to the custom struct.

On \*nix-like operating systems the integration should arrange for proper cleanup if the application is suddenly
terminated (e.g. a crash). The traditional way is to install signal handlers for various fatal signals and do
the cleanup before terminating the application. All functions in termpaint are unsafe for use in signal handlers, so
it's the job of the integration to save all needed information before the signal happens. There are two major parts of
state to restore. The first is the kernel terminal layer configuration, which can simply be saved before changing it to
the needed values for termpaint. The second is the state of the terminal itself that needs to be restored by outputting
a sequence of characters to the terminal. This sequence can change as different features are used, thus the integration
should set a callback via :c:func:`termpaint_integration_set_restore_sequence_updated` and save a copy of that data in
a place where the signal handler can safely access it.

An alternative without installing signal handlers is to use a auxiliary watchdog process to restore the terminal state.
The :doc:`termpaintx addon<termpaintx>` contains functions for such an watchdog process.
See :c:func:`termpaintx_ttyrescue_start_or_nullptr` for details.

Another signal handler is needed to detect terminal size changes. \*nix-like systems raise an ``SIGWINCH`` signal if the
terminal size changes. This signal is best handled asynchronously (e.g. by using an event loop's signal support or using
a self pipe). Outside of signal context the integration can retrieve the new terminal size using the ``TIOCGWINSZ``
ioctl and resize the terminals primary surface to match using :c:func:`termpaint_surface_resize`.

Functions
---------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: void termpaint_integration_init(termpaint_integration *integration, void (*free)(termpaint_integration *integration), void (*write)(termpaint_integration *integration, const char *data, int length), void (*flush)(termpaint_integration *integration))

  This function initializes a ``termpaint_integration`` structure and sets the 3 mandatory callback functions.
  All of the callbacks must be set to a non-NULL value.

  The callbacks are

  ``void (*write)(termpaint_integration *integration, char *data, int length)``

    This callback is called by termpaint to write bytes to the terminal. The application needs to implement this function
    so that ``length`` bytes of data starting at ``data`` are passed to the terminal. The data should be buffered for
    best performance. Termpaint will call the ``flush`` callback when the buffered data needs to be transmitted to
    the terminal.

  ``void (*flush)(termpaint_integration *integration)``

    This callback will be called when the data written using the ``write`` callback needs to be transmitted to the
    terminal.

  ``void (*free)(termpaint_integration *integration)``

    This callback is invoked when the terminal using this integration is deallocated. This function has to be provided,
    but may be just a empty function if the memory of the integration is managed externally.

.. c:function:: void termpaint_integration_deinit(termpaint_integration *integration)

  This function frees resources internally held by a initialized ``termpaint_integration`` structure. It must be called
  exactly once for each ``termpaint_integration`` structure initialized by :c:func:`termpaint_integration_init`.

.. c:function:: void termpaint_integration_set_request_callback(termpaint_integration *integration, void (*request_callback)(termpaint_integration *integration))

  Sets the optional callback ``request_callback``:

  ``void (*request_callback)(termpaint_integration *integration)``

    With terminal input there are often cases where sequences might be finished or just the
    start of a longer sequence. In this case termpaint forces to terminal to output additional data so it can make the
    decision what interpretation is correct. If this callback is set it allows termpaint to delay these commands
    for a short while to wait for additional bytes from the terminal.

    If this callback is implemented the application needs to remember that this callback was called and after a short
    delay (while processing terminal input in the usual way) call :c:func:`termpaint_terminal_callback` on the terminal.
    If this callback is invoked multiple times before the application calls :c:func:`termpaint_terminal_callback` one
    call is sufficient.

    See also :ref:`resync`.

.. c:function:: void termpaint_integration_set_restore_sequence_updated(termpaint_integration *integration, void (*restore_sequence_updated)(termpaint_integration *integration, const char *data, int length))

  Sets the optional callback ``restore_sequence_updated``:

  ``void (*restore_sequence_updated)(termpaint_integration *integration, const char *data, int length)``

    This callback is invoked every time the sequence to reset the terminal changes. This allows to cache a current value
    to be used in crash recovery or suspend signal handlers where :c:func:`termpaint_terminal_restore_sequence` can not
    be used.

    The restore sequence can change over time as additional terminal configuration is requested (e.g. mouse modes,
    set title or global color changes).

.. c:function:: void termpaint_integration_set_is_bad(termpaint_integration *integration, _Bool (*is_bad)(termpaint_integration *integration))

  Sets the optional callback ``is_bad``:

  ``_Bool (*is_bad)(termpaint_integration *integration)``

    This callback should return false, as long as the connection to the terminal is functional.

.. c:function:: void termpaint_integration_set_awaiting_response(termpaint_integration *integration, void (*awaiting_response)(termpaint_integration *integration))

  Sets the optional callback ``awaiting_response``:

  ``void (*awaiting_response)(termpaint_integration *integration)``

    This callback is invoked when termpaint sends queries to the terminal. This can be used to decide if the integration
    should wait for a little while when restoring the terminal while reading and discarding input to avoid leaving
    responses to these queries in flight that might confuse the next application accessing the terminal.

.. c:function:: void termpaint_integration_set_logging_func(termpaint_integration *integration, void (*logging_func)(termpaint_integration *integration, const char *data, int length))

  Sets the optional callback ``logging_func``:

  ``void (*logging_func)(termpaint_integration *integration, const char *data, int length)``

    This callback receives logging messages. Some error messages are always
    logged if this callback is specified. Additional messages can be enabled
    by :c:func:`termpaint_terminal_set_log_mask`.

