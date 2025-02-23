Addon termpaintx
================

termpaintx is an optional add-on to termpaint that offers a very simple
premade integration and a few functions that might be useful for custom
integrations.

For applications that deal with any kind of asynchronous events, apart from
the terminal, it’s recommended to use a custom integration or an external
integration into a general event loop. But for simple applications that only
deal with synchronous data sources and time based events this simple
integration can be used.

termpaintx has operating system specific dependencies and might not be
available for all compilation environments.

The free callback of this integration frees the memory used, thus
termpaint_terminal_new takes ownership of the integration.

..
    For simple synchronous applications termpaintx contains a functional integration. This integration does not support
    additional communication devices or connections.
..
    An example using this integration looks like this::
..
      termpaint_integration *integration = termpaintx_full_integration("+kbdsigint +kbdsigtstp");
      termpaint_terminal *terminal = termpaint_terminal_new(integration);
      termpaint_terminal_set_event_cb(terminal, event_callback, NULL);
      termpaintx_full_integration_set_terminal(integration, terminal);
      termpaint_terminal_auto_detect(terminal);
      termpaintx_full_integration_wait_for_ready(integration);
      termpaintx_full_integration_apply_input_quirks(integration);
      int width, height;
      termpaintx_full_integration_terminal_size(integration, &width, &height);
      termpaint_terminal_setup_fullscreen(terminal, width, height, "+kbdsig");
..
      // use terminal here
..
      while (!quit) {
          if (!termpaint_full_integration_do_iteration(integration)) {
              // some kind of error
              break;
          }
          // either do work here or from the event_callback.
      }
..
      termpaint_terminal_free_with_restore(terminal);

These functions are contained in the header ``termpaintx.h``

.. c:function:: _Bool termpaintx_full_integration_available()

  Checks if the program is connected to a terminal.
  (using `isatty(3) <http://man7.org/linux/man-pages/man3/isatty.3.html>`__)

  This function checks if :c:func:`termpaintx_full_integration` will likely succeed.

.. _termpaintx-options:

.. c:function:: termpaint_integration *termpaintx_full_integration(const char *options)

  Creates an integration object with the given options. It tries finding a connected
  terminal by looking at stdin, stdout, stderr and the process' controlling terminal.

  ``options`` is a space separated list of options.

  Supported options:

    ``+kbdsigint``

      Do not disable kernel keyboard interrupt handling (usually Ctrl-C)

    ``+kbdsigquit``

      Do not disable kernel keyboard quit handling (usually Ctrl-\\)

    ``+kbdsigtstp``

      Do not disable kernel keyboard suspend handling (usually Ctrl-Z)

      This integration does not offer any support for handling the needed SIGTSTP signal for
      proper suspend support. It's your applications responsibility to supply the needed signal
      handling.

      If an application wants to support keyboard suspend it has to arrange for a signal handler
      to restore the terminal and call :c:func:`termpaint_terminal_unpause` after the process
      is resumed and the kernel terminal interface is again configured for termpaint usage.

  Returns NULL on failure.

  Handling of the WINCH (window size changed) signal is automatically setup if possible.

.. c:function:: termpaint_integration *termpaintx_full_integration_from_controlling_terminal(const char *options)

  Creates an integration object with the given options. It tries finding a connected
  terminal by looking at the process' controlling terminal.

  Returns NULL on failure.

  Handling of the WINCH (window size changed) signal is automatically setup.

  See :ref:`here<termpaintx-options>` for allowed values of the ``options`` parameter.

.. c:function:: termpaint_integration *termpaintx_full_integration_from_fd(int fd, _Bool auto_close, const char *options)

  Creates an integration object with the given options. It uses file descriptor ``fd``. If ``auto_close`` is true, the
  file descriptor will be closed when the integration is deallocated.

  The application is responsible to detect terminal size changes and call
  :c:func:`termpaint_surface_resize` on the primary surface with the new size.

  See :ref:`here<termpaintx-options>` for allowed values of the ``options`` parameter.

.. c:function:: termpaint_integration *termpaintx_full_integration_from_fds(int fd_read, int fd_write, const char *options)

  Creates an integration object with the given options. It uses file descriptor ``fd_read`` for reading from the
  terminal and ``fd_write`` for writing to the terminal.

  The application is responsible to detect terminal size changes and call
  :c:func:`termpaint_surface_resize` on the primary surface with the new size.

  See :ref:`here<termpaintx-options>` for allowed values of the ``options`` parameter.

.. c:function:: termpaint_integration *termpaintx_full_integration_setup_terminal_fullscreen(const char *options, void (*event_handler)(void *, termpaint_event *), void *event_handler_user_data, termpaint_terminal **terminal_out)

  Creates an integration and a terminal object with the given options and connects them to work together.
  The integration is returned and the terminal object is made available via the ``terminal`` out-parameter.

  It also runs terminal auto-detection and applies detected input processing quirks, initializes full screen
  mode using :c:func:`termpaint_terminal_setup_fullscreen()` and sets up a watchdog process to restore the terminal to
  it's normal state if the main application suddenly terminates (e.g. a crash).

  The ``event_handler`` and ``event_handler_user_data`` are passed to :c:func:`termpaint_terminal_set_event_cb`.

  Valid options are :ref:`options for termpaint<termpaint-fullscreen-options>` and
  :ref:`options for termpaintx<termpaintx-options>`.

  If the integration can not be initialized then the function prints an error message and returns NULL.

  This function is currently equivalent to a manual setup like this::

    termpaint_integration *integration = termpaintx_full_integration(options);
    if (!integration) {
        const char* error = "Error: Terminal not available!";
        write(1, error, strlen(error));
        return nullptr;
    }

    termpaint_terminal *terminal = termpaint_terminal_new(integration);
    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_terminal_set_event_cb(terminal, event_handler, event_handler_user_data);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");
    termpaintx_full_integration_apply_input_quirks(integration);
    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    termpaint_terminal_setup_fullscreen(terminal, width, height, options);
    termpaintx_full_integration_ttyrescue_start(integration);

.. c:function:: termpaint_integration *termpaintx_full_integration_setup_terminal_inline(const char *options, int lines, void (*event_handler)(void *, termpaint_event *), void *event_handler_user_data, termpaint_terminal **terminal_out)

  Creates an integration and a terminal object with the given options and connects them to work together.
  The integration is returned and the terminal object is made available via the ``terminal`` out-parameter.

  It also runs terminal auto-detection and applies detected input processing quirks, initializes inline
  mode using :c:func:`termpaint_terminal_setup_inline()` and sets up a watchdog process to restore the terminal to
  it's normal state if the main application suddenly terminates (e.g. a crash).

  The height of the inline rendering area is set to ``lines`` high.

  The ``event_handler`` and ``event_handler_user_data`` are passed to :c:func:`termpaint_terminal_set_event_cb`.

  Valid options are :ref:`options for termpaint<termpaint-inline-options>` and
  :ref:`options for termpaintx<termpaintx-options>`.

  If the integration can not be initialized then the function prints an error message and returns NULL.

  The steps are similar to :c:func:`termpaintx_full_integration_setup_terminal_fullscreen` except that
  termpaint_terminal_setup_inline is used and the height passed to it is limited to `lines`.

.. c:function:: _Bool termpaintx_full_integration_do_iteration(termpaint_integration *integration)

  Waits for input from the terminal and passes it to the connected terminal object.

  Return false, if an error occurred while reading from the input file descriptor.

.. c:function:: _Bool termpaintx_full_integration_do_iteration_with_timeout(termpaint_integration *integration, int *milliseconds)

  Waits for input from the terminal for ``*milliseconds`` milliseconds and passes it to the connected terminal object.

  After the call ``*milliseconds`` will contain the remaining milliseconds from the original timeout. If the call
  returned because the timeout expired ``*milliseconds`` will be zero, otherwise it will be the original value minus the
  time spend waiting for and processing input.

  Return false, if an error occurred while reading from the input file descriptor.

.. c:function:: void termpaintx_full_integration_wait_for_ready(termpaint_integration *integration)

  Waits for the auto-detection to be finished. It internally calls :c:func:`termpaintx_full_integration_do_iteration`
  while waiting.

.. c:function:: void termpaintx_full_integration_wait_for_ready_with_message(termpaint_integration *integration, int milliseconds, const char* message)

  Like :c:func:`termpaintx_full_integration_wait_for_ready` but if detection did not finish after `milliseconds`
  milliseconds, will print ``message``.

  Please note, printing a message while fingerprinting is in it's start phase might interfere with fingerprinting. So
  don't use too small values for ``milliseconds``. Nevertheless a timeout can help for terminals that are not compatible
  with running terminal autodetection, by at least altering the user that something likly has gone wrong.

.. c:function:: void termpaintx_full_integration_apply_input_quirks(termpaint_integration *integration)

  Setup input handling based on the auto detection result and tty parameters.

  Needs to be called after auto detection is finished.

  It internally calls :c:func:`termpaint_terminal_auto_detect_apply_input_quirks`

.. c:function:: void termpaintx_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal)

  Sets the terminal object to be managed by this integration object. This needs to be called before using
  :c:func:`termpaintx_full_integration_do_iteration` when not using
  :c:func:`termpaintx_full_integration_setup_terminal_fullscreen` (which already does that).

.. c:function:: const struct termios *termpaintx_full_integration_original_terminal_attributes(termpaint_integration *integration)

  Returns a pointer to the saved terminal attributes in ``termios`` format. The pointer is valid until the integration
  is freed.

  Note: As all functions in termpaint this function is not async-signal safe. If the application needs this information
  in a signal handler it needs to call this function while initializing and store the value for the signal handler to use.

.. c:function:: void termpaintx_full_integration_set_inline(termpaint_integration *integration, _Bool enabled, int height)

  Sets the inline status and inline height for the associated terminal.

  If ``enabled`` is true, the terminal is set to inline mode and resized to the height given in ``height`` (or the
  size of the connected terminal if ``height`` is larger).

  In contrast to :c:func:`termpaint_terminal_set_inline` this function updates essential state in termpaintx needed for
  handling terminal resize.

  A call to this function must be followed by reestablishing the primary surface contents and
  by a call to :c:func:`termpaint_terminal_flush()` to finalize the switch to the new mode.

  On manual terminal initializations (i.e. using :c:func:`termpaint_terminal_setup_inline` instead of
  :c:func:`termpaintx_full_integration_setup_terminal_inline`) of inline mode, this function may also be used to
  synchronize termpaintx internal state to inline state of termpaint, if used with ``enabled`` is true and ``height`` is
  greater than zero.

.. c:function:: _Bool termpaintx_full_integration_ttyrescue_start(termpaint_integration *integration)

  Sets up a watchdog process to restore the terminal to it’s normal state if the
  main application suddenly terminates (e.g. a crash).

  Returns false on failure.

.. c:function:: _Bool termpaintx_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height)

  Stores the current terminal size into ``*width`` and ``*height``. This function relies on the terminal size cached in
  the kernel.

  Returns false on failure.

.. c:function:: termpaint_logging_func termpaintx_enable_tk_logging(void)

  This function starts a helper process that uses python3 to create a window (using
  `tkinter <https://docs.python.org/3/library/tkinter.html>`_ with logging messages. The window will appear when
  the first log message is output.

  It returns a function suitable as logging callback for a integration.

  If an error occurred setting up the helper process returns a no-op logging function.

  This function is meant to easy development and debugging of an application using termpaint. It's not meant
  for usage in the final application.

  This function is only available if enabled at compile time.

Functions for custom integrations
---------------------------------

.. c:function:: _Bool termpaintx_fd_set_termios(int fd, const char *options)

  This function can be used to get the kernel terminal setup without using the full integration.
  Instead of a pointer to an integration object this accesses the terminal directly by the file
  descriptor ``fd``.

  It accepts the same options as :c:func:`termpaintx_full_integration`

  Returns false on failure.

.. c:function:: _Bool termpaintx_fd_terminal_size(int fd, int *width, int *height)

  This function can be used to get the terminal size from the kernel without using the full integration.
  Instead of a pointer to an integration object this accesses the terminal directly by the file
  descriptor ``fd``.

  Otherwise it works like :c:func:`termpaintx_full_integration_terminal_size`.

  Returns false on failure.

Terminal restore watchdog
-------------------------

.. c:type:: termpaintx_ttyrescue

termpaintx has a functions to create a watchdog subprocess to restore the terminal to a usable state
on sudden program termination (e.g. a crash).

This watchdog process uses a socket pair (similar to a pipe) to monitor that the main process is
still running. If the main process terminates without first signaling a clean shutdown by calling
:c:func:`termpaintx_ttyrescue_stop` the watchdog restores the terminal and kernel interface settings.

When using the integration from termpaintx the watchdog is started by calling
:c:func:`termpaintx_full_integration_ttyrescue_start`. The integration takes care of updating
the restore sequence as it changes over time and communicating the original kernel terminal
interface layer settings to the watchdog. The watchdog is automatically shut down, when the
integration is freed.

If the watchdog is used with a custom terminal integration it is started using
:c:func:`termpaintx_ttyrescue_start_or_nullptr`, passing it the initial restore sequence and the file
descriptor of the terminal. The integration has to call
:c:func:`termpaintx_ttyrescue_set_restore_termios` to set the original ``struct termios``
contents and if the restore sequence changes it has to call :c:func:`termpaintx_ttyrescue_update`
with the new restore sequence.

Functions
.........

These functions are contained in the header ``termpaintx_ttyrescue.h``

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: termpaintx_ttyrescue *termpaintx_ttyrescue_start_or_nullptr(int fd, const char *restore_seq)

  Setup the watchdog process. The watchdog uses terminal file descriptor ``fd`` when a restore is triggered,
  sending the string ``restore_seq`` to the terminal.

  Returns ``NULL`` on error.

.. c:function:: void termpaintx_ttyrescue_stop(termpaintx_ttyrescue *tpr)

  Cleanly stop the watchdog process.

.. c:function:: _Bool termpaintx_ttyrescue_update(termpaintx_ttyrescue *tpr, const char* data, int len)

  Update the restore sequence used by the watchdog process.

  Returns false on failure.

.. c:function:: _Bool termpaintx_ttyrescue_set_restore_termios(termpaintx_ttyrescue *tpr, const struct termios *original_terminal_attributes)

  Set or update the ``struct termios`` to reset the terminal kernel interface to when the watchdog triggers.

  Returns false on failure.

