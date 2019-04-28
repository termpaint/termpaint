Integration
===========

.. c:type:: termpaint_integration

The core termpaint library does not come with integration into the operating systems input/output channels. It's desiged
to be integrated into synchronous or asynchronous program designs.

There are auxillary functions in the termpaintx_* namespace that have some common code that can be used for integrations.
But this code is fairly limited, so if more capabilities are needed feel free to copy this code into your project.

The integration consists of 3 major parts: terminal to termpaint communication, termpaint to terminal communication and
terminal interface setup.

Termpaint uses a struct with callbacks for calling integration code when needed. The :c:type:`termpaint_integration`
structure contains the following callbacks:

  .. c:function:: void (*write)(struct termpaint_integration *integration, char *data, int length)

    This callback is called by termpaint to write bytes to the terminal. The application needs to implement this function
    so that ``length`` bytes of data starting at ``data`` are passed to the terminal. The data may be buffered. Termpaint
    will call the ``flush`` callback when the buffered data needs to be transmitted to the terminal.

  .. c:function:: void (*flush)(struct termpaint_integration *integration)

    This callback will be called when the data written using the ``write`` callback needs to be transmitted to the
    terminal.

  .. c:function:: void (*request_callback)(struct termpaint_integration *integration)

    This callback is optional. With terminal input there are often cases where sequences might be finished or just the
    start of a longer sequence. In this case termpaint forces to terminal to output additional data so it can make the
    decision what interpretation is right. If this callback is set it allows the application to delay these commands for
    a short while to wait for additional bytes from the terminal.

    If this callback is implemented the application needs to remember that this callback was called and after a short
    delay (while processing terminal input in the usual way) call :c:func:`termpaint_terminal_callback` on the terminal.
    If this callback is invoked multiple times before the application calls :c:func:`termpaint_terminal_callback` one
    call is sufficent.

  .. c:function:: void (*free)(struct termpaint_integration *integration)

    This callback is invoked when the terminal using this integration is deallocated. This function has to be provided,
    but may be just a empty function if the memory of the integration is managed externally.

  .. c:function:: _Bool (*is_bad)(struct termpaint_integration *integration)

    This callback should return true, as long as the connection to the terminal is functional.

A pointer to the :c:type:`termpaint_integration` is passed to :c:func:`termpaint_terminal_new` when creating the terminal
object.

Input bytes from the terminal to termpaint need to be passed to :c:func:`termpaint_terminal_add_input_data`. If enough
bytes have accumulated to identify a input sequence termpaint will call the event callback set by the application using
:c:func:`termpaint_terminal_set_event_cb` with the interpreted :doc:`event <events>`.

Some platforms have kernel level terminal processing that needs to be configured for termpaint to work. On \*nix like
platforms the kernel tty interface can be setup with :c:func:`termpaintx_fd_set_termios`. For details see the
implementation of that function. In general the terminal interface should be set to disable all kernel interpretation
and transformation features. If keyboard signal handling (ctrl-c, etc) is needed it can be left enabled. But in that
case the terminal object needs to be configured with ``+kbdsig`` to avoid switching keyboard input into advanced modes
that would be incompatible with kernel signal generation.

In addition to the kernel interface the terminal needs to be setup using configuration sequences. For this
:c:func:`termpaint_terminal_setup_fullscreen` needs to be called with the size of the terminal.

The terminal object works better with terminal specific setup which can enabled by doing a terminal auto-detection before
calling :c:func:`termpaint_terminal_setup_fullscreen`. The terminal auto-detection can be started using
:c:func:`termpaint_terminal_auto_detect`. This will initiate bidirectional communication to the terminal. The application
can proceed with the setup when the detection is finished.

For applications prefering synchronous integration the application should call
:c:func:`termpaint_terminal_auto_detect_state` after each additional input from the terminal. If this function returns
``termpaint_auto_detect_done`` the detection is finished.

For applications prefering asynchronous integration the application needs to wait for an event of type
:c:macro:`TERMPAINT_EV_AUTO_DETECT_FINISHED` before proceeding with terminal setup.

In either case the application needs to set an event callback before starting auto-detection.

When the application terminates it needs to restore both terminal configuration as well as the kernel level terminal
setup back to it's previous values. The first part should be done by calling
:c:func:`termpaint_terminal_free_with_restore`. The second part should be done by using operating system specific calls
to save the kernel settings before changing those and then restoring them after restoring the terminal setup.

For simple synchronous applications termpaintx contains a functional integration. This integration does not support
timed events or additional communication devices or connections.

An example using this integration looks like this::

  termpaint_terminal *terminal = termpaint_terminal_new(integration);
  termpaint_terminal_set_event_cb(terminal, event_callback, NULL);
  termpaint_full_integration_set_terminal(integration, terminal);
  termpaint_terminal_auto_detect(terminal);
  termpaint_full_integration_wait_for_ready(integration);
  int width, height;
  termpaint_full_integration_terminal_size(integration, &width, &height);
  termpaint_terminal_setup_fullscreen(terminal, width, height, "+kbdsig");

  // use terminal here

  while (!quit) {
      if (!termpaint_full_integration_do_iteration(integration)) {
          // some kind of error
          break;
      }
      // either do work here or from the event_callback.
  }

  termpaint_terminal_free_with_restore(terminal);

TODO document termpaint_ttyrescue
