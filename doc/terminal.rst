Terminal
========

.. c:type:: termpaint_terminal

The terminal object represents a connected terminal. This can be seen as the root object in termpaint.

The actual connection with the terminal is left to the application. It can be connected to both a operating system
terminal as well as to network protocols with suitable glue.

The terminal always needs an bi-directional connection for the terminal object to work.

See :doc:`integration` for how to integrate termpaint into an application.

Terminals have different capabilities and thus a auto detection step is recommended before using the terminal
in the application.

Input and other events from the terminal as passed to the application using callback functions.
The primary callback is the event callback which is called for keyboard input, mouse events and clipboard
paste events. Use :c:func:`termpaint_terminal_set_event_cb()` to setup your event callback.

Output is done by placing text on the primary surface of the terminal. This surface can be obtained by
:c:func:`termpaint_terminal_get_surface()`. After all text is in place, call :c:func:`termpaint_terminal_flush()`
to output the contents of the surface to the terminal.

Functions
---------

.. c:function:: termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration)

  Create a new terminal object.

  See :doc:`integration` for details on the callbacks needed in ``integration``.

  The application has to free this with :c:func:`termpaint_terminal_free_with_restore()` or
  :c:func:`termpaint_terminal_free()`

.. c:function:: void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options)

  Setup the terminal connected to the terminal object ``term`` to fullscreen mode. Assume terminal size is ``width``
  columns by ``height`` lines.

  ``options`` specifies an space delimited list of additional settings:

    ``-altscreen``
      Do not activate the alternative screen of the terminal. Previous contents of the screen is not restored after
      terminating the application.

    ``+kbdsig``
      Do not activate any modes of the terminal that might conflict with processing of keyboard signals in the kernel
      tty layer. Use this when passing ``+kdbsig`` to TODO(integration init).

      Affected key combinations are usually ctrl-c, ctrl-z and, ctrl-\\

.. c:function:: void termpaint_terminal_free_with_restore(termpaint_terminal *term)

  Frees the terminal object ``term`` and restores the attached terminal to it's base state.

.. c:function:: void termpaint_terminal_free(termpaint_terminal *term)

  Frees the terminal object ``term`` without restoring the attached terminal to it's base state. This will leave the
  terminal in a state that other applications or the shell will not be prepared to handle in most cases.

  Prefer to use :c:func:`termpaint_terminal_free_with_restore()`.

.. c:function:: termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term)

  Returns the primary surface of the terminal object ``term``. This surface is linked to the terminal and can be
  output using :c:func:`termpaint_terminal_flush`.

  This object is owned by the terminal object, don't free the returned value.

.. c:function:: void termpaint_terminal_flush(termpaint_terminal *term, bool full_repaint)

  Output the current state of the primary surface of the terminal object to the attached terminal.

  If ``full_repaint`` is false it uses incremental drawing to reduce bandwidth use. Else it does a full redraw
  that can repair the contents of the terminal in case another application interfered with uncoordinated output
  to the same underlying terminal.

.. c:function:: const char *termpaint_terminal_restore_sequence(const termpaint_terminal *term)

  Returns a null terminated string that can be used to restore the terminal to it's base state.

  TODO: Discuss restrictions with regard to termpaint_terminal_free_with_restore

.. c:function:: void termpaint_terminal_set_cursor_position(termpaint_terminal *term, int x, int y)

  Sets the text cursor position for the terminal object ``term``. The cursor is moved to this position
  the next time :c:func:`termpaint_terminal_flush` is called.

.. c:function:: void termpaint_terminal_set_cursor_visible(termpaint_terminal *term, bool visible)

  Sets the visibility of the text cursor for the terminal object ``term``. The cursor is shown/hidden
  the next time :c:func:`termpaint_terminal_flush` is called.


.. c:function:: void termpaint_terminal_set_cursor_style(termpaint_terminal *term, int style, bool blink)

  Sets the cursor shape / style of the terminal object ``term`` to the style specified in ``style``.
  If ``blink`` is true, the cursor will blink.

  The following styles are available:

    .. c:macro:: TERMPAINT_CURSOR_STYLE_TERM_DEFAULT

        This is the terminal default style. This is terminal implementation and configuration defined.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_BLOCK

        Display the cursor as a block that covers an entire character.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_UNDERLINE

        Display the cursor as a underline under the character.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_BAR

        Display the cursor as a vertical bar between characters.


.. c:function:: void termpaint_terminal_set_color(termpaint_terminal *term, int color_slot, int r, int b, int g)

  TODO

.. c:function:: void termpaint_terminal_reset_color(termpaint_terminal *term, int color_slot)

  TODO

.. c:function:: void termpaint_terminal_callback(termpaint_terminal *term)

  If the application has set ``request_callback`` in the integration structure, this function needs to be called after
  a delay when the terminal object requests it by invoking the ``request_callback`` callback.

.. c:function:: void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, bool (*cb)(void *user_data, const char *data, unsigned length, bool overflow), void *user_data)

  This function allows settings a callback that is called with raw sequences before interpretation. The application can
  inspect the sequences in this callback. If the callback returns true the sequence is not interpreted further.

.. c:function:: void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *user_data, termpaint_event* event), void *user_data)

  The application must use this function to set an event callback. See :doc:`events` for details about events produced
  by terminal input.

.. c:function:: void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length)

  The integration part of the application has to call this function to pass terminal input data to the terminal object.
  See :doc:`integration` for details.

.. c:function:: const char* termpaint_terminal_peek_input_buffer(const termpaint_terminal *term)

  This function in conjunction with :c:func:`termpaint_terminal_peek_input_buffer_length` allows an application
  to observe input data that is buffered by not yet processed. If called after :c:func:`termpaint_terminal_add_input_data`
  returned, this will contain data in partial or ambiguous sequences not yet processed.

.. c:function:: int termpaint_terminal_peek_input_buffer_length(const termpaint_terminal *term)

  Returns the length of the valid data for :c:func:`termpaint_terminal_peek_input_buffer`.

.. c:function:: bool termpaint_terminal_auto_detect(termpaint_terminal *terminal)

  Starts terminal type auto-detection. The event callback has to be set before calling this function.

.. c:type:: enum termpaint_auto_detect_state_enum
    { termpaint_auto_detect_none, termpaint_auto_detect_running, termpaint_auto_detect_done }

.. c:function:: enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(const termpaint_terminal *terminal)

  Get the state of a possibly running terminal type auto-detection.

.. c:function:: void termpaint_terminal_auto_detect_apply_input_quirks(termpaint_terminal *terminal, _Bool backspace_is_x08)

  Setup input handling based on the auto detection result and ``backspace_is_x08``.

  Needs to be called after auto detection is finished.

  Pass ``backspace_is_x08`` as true if the terminal uses 0x08 (ASCII BS) for the backspace key.

  On \*nix platforms this information can be obtained from the ``termios`` structure by ``original_termios.c_cc[VERASE] == 0x08``.
  For ssh connections the VERASE value is transmitted as part of the pseudo terminal request in the encoded
  terminal modes.

.. c:function:: void termpaint_terminal_auto_detect_result_text(const termpaint_terminal *terminal, char *buffer, int buffer_length)

  Fills ``buffer`` with null terminaled string with debugging details about the detected terminal type.
  The buffer pointed to by ``buffer`` needs to be at least ``buffer_length`` bytes long.


