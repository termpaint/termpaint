Terminal
========

.. c:type:: termpaint_terminal

The terminal object represents a connected terminal. This can be seen as the root object in termpaint.

The actual connection with the terminal is left to the application. It can be connected to both a operating system
terminal as well as to network protocols with suitable glue.

The terminal always needs an bi-directional connection for the terminal object to work.

See :doc:`integration` for how write a custom integrate of termpaint into an application.

See :doc:`termpaintx` for a simple premade integration suitable for simple applications using a synchronous
usage style or which work with a very simple event loop design.

Terminals have different capabilities and thus a auto detection step is recommended before using the terminal
in the application.

Input and other events from the terminal are passed to the application using callback functions.
The primary callback is the event callback which is called for keyboard input, mouse events and clipboard
paste events. Use :c:func:`termpaint_terminal_set_event_cb()` to setup your event callback. This callback
is required.

Output is done by placing text on the primary surface of the terminal. This surface can be obtained by
:c:func:`termpaint_terminal_get_surface()`. After all text is in place, call :c:func:`termpaint_terminal_flush()`
to output the contents of the surface to the terminal. If no full repaint is requested this will only send
:ref:`differences<incremental-update>` since the last flush to the terminal.


.. _terminal-setup:

Setup
-----

The terminal object works better with terminal specific setup which can enabled by doing a terminal auto-detection before
calling :c:func:`termpaint_terminal_setup_fullscreen`. The terminal auto-detection can be started using
:c:func:`termpaint_terminal_auto_detect`. This will initiate bidirectional communication to the terminal. The application
can proceed with the setup when the detection is finished.

For applications preferring synchronous integration the application should call
:c:func:`termpaint_terminal_auto_detect_state` after each additional input from the terminal. If this function returns
``termpaint_auto_detect_done`` the detection is finished.

For applications preferring asynchronous integration the application needs to wait for an event of type
:c:macro:`TERMPAINT_EV_AUTO_DETECT_FINISHED` before proceeding with terminal setup.

In either case the application needs to set an event callback before starting auto-detection.

When the application terminates it needs to restore both terminal configuration as well as the kernel level terminal
setup back to it's previous values. The first part should be done by calling
:c:func:`termpaint_terminal_free_with_restore`. The second part should be done by using operating system specific calls
to save the kernel settings before changing those and then restoring them after restoring the terminal setup.

Common functions
----------------

These functions are commonly used when a initialized terminal object has been acquired.

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: void termpaint_terminal_free_with_restore(termpaint_terminal *term)

  Frees the terminal object ``term`` and restores the attached terminal to it's base state.

  This function calls the integrations free callback.

.. c:function:: void termpaint_terminal_free(termpaint_terminal *term)

  Frees the terminal object ``term`` without restoring the attached terminal to it's base state. This will leave the
  terminal in a state that other applications or the shell will not be prepared to handle in most cases.

  Prefer to use :c:func:`termpaint_terminal_free_with_restore()`.

  This function calls the integrations free callback.

.. c:function:: void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *user_data, termpaint_event* event), void *user_data)

  The application must use this function to set an event callback. See :doc:`events` for details about events produced
  by terminal input.

  This is mostly a wrapper for using :c:func:`termpaint_input_set_event_cb` with a terminal object. Termpaint interprets
  certain events before passing them on to the application. Also, while running terminal auto detection, events are not
  passed to the given callback. Some events like :c:macro:`TERMPAINT_EV_AUTO_DETECT_FINISHED` are actually produced by
  termpaint and not by termpaint_input.

.. c:function:: termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term)

  Returns the primary surface of the terminal object ``term``. This surface is linked to the terminal and can be
  output using :c:func:`termpaint_terminal_flush`.

  This object is owned by the terminal object, don't free the returned value.

.. c:function:: void termpaint_terminal_flush(termpaint_terminal *term, bool full_repaint)

  Output the current state of the primary surface of the terminal object to the attached terminal.

  If ``full_repaint`` is false it uses :ref:`incremental drawing<incremental-update>` to reduce bandwidth use.
  Else it does a full redraw that can repair the contents of the terminal in case another application
  interfered with uncoordinated output to the same underlying terminal.

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

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_CURSOR_STYLE_TERM_DEFAULT

        This is the terminal default style. This is terminal implementation and configuration defined.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_BLOCK

        Display the cursor as a block that covers an entire character.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_UNDERLINE

        Display the cursor as a underline under the character.

    .. c:macro:: TERMPAINT_CURSOR_STYLE_BAR

        Display the cursor as a vertical bar between characters.

.. c:function:: void termpaint_terminal_set_title(termpaint_terminal *term, const char* title, int mode)

  Set the title of the terminal to the string ``title``. ``mode`` specifies how to handle terminals where
  it is not certain that the original title can be restored when exiting the application.

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_TITLE_MODE_ENSURE_RESTORE

      Only set the title if it is certain that the original title can be restored when the application restores
      the terminal.

      This is the recommended mode.

    .. c:macro:: TERMPAINT_TITLE_MODE_PREFER_RESTORE

      Set the title on all terminals that support setting a title without restricting to terminals that are known
      to be able to restore the title when the application restores the terminal.

.. c:function:: void termpaint_terminal_set_icon_title(termpaint_terminal *term, const char* title, int mode)

  This function is like :c:func:`termpaint_terminal_set_title` but does not set the primary title but an alternative
  title called the icon title. Interpretation of this title differs by terminal.

.. c:function:: void termpaint_terminal_set_color(termpaint_terminal *term, int color_slot, int r, int b, int g)

  Set special global (not per cell) terminal colors.

  ``color_slot`` can be one of:

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_COLOR_SLOT_BACKGROUND

      This is the color used for cells without an explicitly set background. This color is used e.g. for cells
      using the :c:macro:`TERMPAINT_DEFAULT_COLOR` as background.

    .. c:macro:: TERMPAINT_COLOR_SLOT_FOREGRUND

      This is the color used for cells without an explicitly set foreground. This color is used e.g. for cells
      using the :c:macro:`TERMPAINT_DEFAULT_COLOR` as foreground.

    .. c:macro:: TERMPAINT_COLOR_SLOT_CURSOR

      Set the color of the cursor in the terminal.

.. c:function:: void termpaint_terminal_reset_color(termpaint_terminal *term, int color_slot)

  Reset color choices made using :c:func:`termpaint_terminal_set_color`. When ``color_slot`` is
  ``TERMPAINT_COLOR_SLOT_CURSOR`` the cursor color is reset to the default.

.. c:function:: void termpaint_terminal_request_tagged_paste(termpaint_terminal *term, _Bool enabled)

  Request the terminal to send the needed information so :c:macro:`TERMPAINT_EV_PASTE` events can be generated.

.. c:function:: void termpaint_terminal_set_mouse_mode(termpaint_terminal *term, int mouse_mode)

  Request the terminal to enable mouse handling by the application. Depending on the setting
  :c:macro:`TERMPAINT_EV_MOUSE` events will be generated for:

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_MOUSE_MODE_OFF

        No events.

        Terminal native select and copy features will be available to the user.

    .. c:macro:: TERMPAINT_MOUSE_MODE_CLICKS

        Only report mouse down and up events (clicks).

        Terminal native select and copy features will not be available to the user. Some terminals allow
        overriding this mouse mode using shift temporarily.

    .. c:macro:: TERMPAINT_MOUSE_MODE_DRAG

        Report mouse down and up events as well as movement when at least one mouse button is held down.

        Terminal native select and copy features will not be available to the user. Some terminals allow
        overriding this mouse mode using shift temporarily.

    .. c:macro:: TERMPAINT_MOUSE_MODE_MOVEMENT

        Report mouse movement and down and up events independent of mouse button state.

        Terminal native select and copy features will not be available to the user. Some terminals allow
        overriding this mouse mode using shift temporarily.

.. c:function:: void termpaint_terminal_request_focus_change_reports(termpaint_terminal *term, _Bool enabled)

  Request focus change events from the terminal. If supported by the terminal these events will be reported
  as :ref:`misc-events` of type :c:func:`termpaint_input_focus_in` and :c:func:`termpaint_input_focus_out`.

.. c:function:: _Bool termpaint_terminal_should_use_truecolor(termpaint_terminal *terminal)

  After auto detection, returns true if termpaint does not translate rgb color colors to indexed colors.

  To force passing rgb colors to the terminal, one of the the capabilities
  :c:macro:`TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED`
  or :c:macro:`TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED` must be set.


.. c:function:: void termpaint_terminal_bell(termpaint_terminal *term)

  Send the BEL character to the terminal. Most terminals trigger a visual or audio reaction to the BEL character.


Functions for setup and auto-detection
--------------------------------------

These functions are used to get a initialized terminal object and are somewhat dependent on the integration used.

For the integration from the :doc:`termpaintx addon<termpaintx>` a convenience function encapsulating the setup is
available as :c:func:`termpaintx_full_integration_setup_terminal_fullscreen` that can be used instead.

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration)

  Create a new terminal object.

  See :doc:`integration` for details on the callbacks needed in ``integration``.

  The application has to free this with :c:func:`termpaint_terminal_free_with_restore()` or
  :c:func:`termpaint_terminal_free()`

  If the integration's free callback frees the integration this takes ownership of the integration.


.. c:function:: bool termpaint_terminal_auto_detect(termpaint_terminal *terminal)

  Starts terminal type auto-detection. The event callback has to be set before calling this function.

  Return false, if the auto-detection could not be started.

.. c:enum:: termpaint_auto_detect_state_enum

  .. c:namespace:: 0
  .. c:enumerator:: termpaint_auto_detect_none

    Terminal type auto-detection was not run yet.

  .. c:enumerator:: termpaint_auto_detect_running

    Terminal type auto-detection is currently running.

  .. c:enumerator:: termpaint_auto_detect_done

    Terminal type auto-detection was run and has finished.

.. c:function:: enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(const termpaint_terminal *terminal)

  Get the state of a possibly running terminal type auto-detection.

.. c:function:: _Bool termpaint_terminal_might_be_supported(const termpaint_terminal *terminal)

  After auto detection, returns true if the terminal might be supported. If it returns false the terminal
  is likely missing essential features for proper support.

.. _termpaint-fullscreen-options:

.. c:function:: void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options)

  Setup the terminal connected to the terminal object ``term`` to fullscreen mode. Assume terminal size is ``width``
  columns by ``height`` lines.

  ``options`` specifies an space delimited list of additional settings:

    ``-altscreen``
      Do not activate the alternative screen of the terminal. Previous contents of the screen is not restored after
      terminating the application.

    ``+kbdsig``
      Do not activate any modes of the terminal that might conflict with processing of keyboard signals in the kernel
      tty layer. Use this when passing ``+kdbsigint``, ``+kdbsigquit`` or ``+kdbsigtstp`` to
      :c:func:`termpaintx_full_integration` or when using an custom integration that enabled the equivalent kernel
      terminal layer processing.

      Affected key combinations are usually ctrl-c, ctrl-z and, ctrl-\\

.. c:function:: void termpaint_terminal_auto_detect_apply_input_quirks(termpaint_terminal *terminal, _Bool backspace_is_x08)

  Setup input handling based on the auto detection result and ``backspace_is_x08``.

  Needs to be called after auto detection is finished.

  Pass ``backspace_is_x08`` as true if the terminal uses 0x08 (ASCII BS) for the backspace key.

  On \*nix platforms this information can be obtained from the ``termios`` structure by ``original_termios.c_cc[VERASE] == 0x08``.
  For ssh connections the VERASE value is transmitted as part of the pseudo terminal request in the encoded
  terminal modes.


Special purpose functions
-------------------------

These functions have specialized use. They are not needed in many applications.

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: void termpaint_terminal_pause(termpaint_terminal *term)

  Temporarily restore the terminal state. This should be called before running external applications.
  To return to rendering by termpaint call :c:func:`termpaint_terminal_unpause`.

  After calling this function the application still needs to restore the kernel tty layer settings
  to the state needed to run external applications.

.. c:function:: void termpaint_terminal_unpause(termpaint_terminal *term)

  This function activates termpaint mode again after it was previously temporarily restored to the
  normal state.

  Before calling this function the application needs to restore the kernel tty layer settings to
  the state needed by termpaint (or to the state before calling pause).

.. c:function:: void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, bool (*cb)(void *user_data, const char *data, unsigned length, bool overflow), void *user_data)

  This function allows settings a callback that is called with raw sequences before interpretation. The application can
  inspect the sequences in this callback. If the callback returns true the sequence is not interpreted further.

  This is mostly a wrapper for using :c:func:`termpaint_input_set_raw_filter_cb` with a terminal object. But events
  while running terminal auto detection are not passed to the given callback.

.. c:function:: void termpaint_terminal_handle_paste(termpaint_terminal *term, _Bool enabled)

  This is a wrapper for using :c:func:`termpaint_input_handle_paste` with a terminal object.

  Explicit paste handling is an switchable terminal feature, see
  :c:func:`termpaint_terminal_request_tagged_paste` for enabling it.


.. c:function:: const char *termpaint_terminal_self_reported_name_and_version(const termpaint_terminal *terminal)

  Returns a pointer to a string with the result of the terminal's self reported name and version. Only some terminals
  support this. For other terminals NULL will be returned.

  This value is only available after successful terminal auto-detection. The returned pointer is valid until the
  terminal object is freed or terminal auto detection is triggered again.

.. c:function:: void termpaint_terminal_auto_detect_result_text(const termpaint_terminal *terminal, char *buffer, int buffer_length)

  Fills ``buffer`` with null terminated string with debugging details about the detected terminal type.
  The buffer pointed to by ``buffer`` needs to be at least ``buffer_length`` bytes long.

.. c:function:: void termpaint_terminal_activate_input_quirk(termpaint_terminal *term, int quirk)

  This is a wrapper for using :c:func:`termpaint_input_activate_quirk` with a terminal object.

  Quirks matching the auto detected terminal are already activated by
  :c:func:`termpaint_terminal_auto_detect_apply_input_quirks`.

  Calling this function explicitly should be rarely needed.

.. c:function:: const char* termpaint_terminal_peek_input_buffer(const termpaint_terminal *term)

  This function in conjunction with :c:func:`termpaint_terminal_peek_input_buffer_length` allows an application
  to observe input data that is buffered by not yet processed. If called after :c:func:`termpaint_terminal_add_input_data`
  returned, this will contain data in partial or ambiguous sequences not yet processed.

  This is a wrapper for using :c:func:`termpaint_input_peek_buffer` with a terminal object.

.. c:function:: int termpaint_terminal_peek_input_buffer_length(const termpaint_terminal *term)

  Returns the length of the valid data for :c:func:`termpaint_terminal_peek_input_buffer`.

  This is a wrapper for using :c:func:`termpaint_input_peek_buffer_length` with a terminal object.

.. c:function:: void termpaint_terminal_set_log_mask(termpaint_terminal *term, unsigned mask)

  Set the mask of what besides errors is reported to the integration's logging callback.

  All logging messages are for debugging only and might change between releases.

  ``mask`` is a bit combination of

  .. c:namespace:: 0
  .. c:macro:: TERMPAINT_LOG_AUTO_DETECT_TRACE

    Log details of the auto detection state machine

  .. c:macro:: TERMPAINT_LOG_TRACE_RAW_INPUT

    Log raw input bytes from the terminal.


.. c:function:: _Bool termpaint_terminal_capable(const termpaint_terminal *terminal, int capability)

  Features supported differ among terminal implementations. Termpaint uses as set of capabilities to decide
  how to interface with terminals. This function allows to query currently set capabilities.

  Capabilities start with some defaults and get setup during terminal auto-detection.

  The following capabilities are available:

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_CAPABILITY_7BIT_ST

        The terminals fully supports using ``ESC\\`` as string terminator. This is the string terminator
        specified by ECMA-48.

    .. c:macro:: TERMPAINT_CAPABILITY_88_COLOR

        The terminal uses 88 colors for indexed colors instead of the more widely supported 256 colors.

    .. c:macro:: TERMPAINT_CAPABILITY_CLEARED_COLORING

        The terminal supports using "clear to end of line" for trailing sequences of insignificant
        spaces. This includes support for setting up multiple colored ranges per line using this
        sequence.

    .. c:macro:: TERMPAINT_CAPABILITY_CLEARED_COLORING_DEFCOLOR

        If TERMPAINT_CAPABILITY_CLEARED_COLORING is supported this indicated if this sequence also
        works for the special "default" terminal color.

    .. c:macro:: TERMPAINT_CAPABILITY_CSI_EQUALS

        The terminal's escape sequence parser properly handles sequences starting with ``ESC[=``
        and ignores unknown sequences of this type.

    .. c:macro:: TERMPAINT_CAPABILITY_CSI_GREATER

        The terminal's escape sequence parser properly handles sequences starting with ``ESC[>``
        and ignores unknown sequences of this type.

    .. c:macro:: TERMPAINT_CAPABILITY_CSI_POSTFIX_MOD

        The terminal's escape sequence parser properly handles sequences that use a intermediate
        character before the final character of a CSI sequence.

    .. c:macro:: TERMPAINT_CAPABILITY_CURSOR_SHAPE_OSC50

        Cursor shape needs to be setup with a konsole specific escape sequence.

    .. c:macro:: TERMPAINT_CAPABILITY_EXTENDED_CHARSET

        The terminal is capable of displaying a font with more than 512 different characters.

    .. c:macro:: TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE

        The terminal's parser is expected to cope with the cursor setup CSI sequence without
        glitches.

    .. c:macro:: TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR

        The terminal either does not support cursor shapes or it does support bar cursor shape.

    .. c:macro:: TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE

        The terminal supports bracketed/tagged paste.

    .. c:macro:: TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT

        The terminal uses a format for cursor position reports that is distinct from key press reports.

    .. c:macro:: TERMPAINT_CAPABILITY_TITLE_RESTORE

        The terminal has a title stack that can be used to restore the title.

    .. c:macro:: TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED

        The terminal is not known to have problems with rgb(truecolor) color types.

    .. c:macro:: TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED

        The terminal is known to support rgb(truecolor) color types.

.. c:function:: void termpaint_terminal_promise_capability(termpaint_terminal *terminal, int capability)

  This function allows overriding terminal type auto-detection of terminal capabilities.

  Use this with care, if the terminal is not able to handle the enabled capabilities the rendering might
  break.

.. c:function:: void termpaint_terminal_disable_capability(termpaint_terminal *terminal, int capability)

  This function allows overriding terminal type auto-detection of terminal capabilities.

  On specific use is to disable :c:macro:`TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED` to switch to
  a more conservative estamination of a terminals capability to support rgb color modes.

.. c:function:: void termpaint_terminal_expect_apc_input_sequences(termpaint_terminal *term, _Bool enabled)

  This is a wrapper for using :c:func:`termpaint_input_expect_apc_sequences` with a terminal object.

  APC sequences are only known to be used by kitty in an extended keyboard reporting mode that is currently
  not supported by termpaint.

.. c:function:: void termpaint_terminal_expect_cursor_position_report(termpaint_terminal *term)

  This is a wrapper for using :c:func:`termpaint_input_expect_cursor_position_report` with a terminal object.

  Needs to be called for each ``ESC[6n`` sequence send manually to the terminal to ensure the result is
  interpreted as cursor position report instead of a key press.

  If the terminal :c:macro:`properly supports ESC[?6n<TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT>` that sequence
  should be used and this function does
  not need to be called.

.. c:function:: void termpaint_terminal_expect_legacy_mouse_reports(termpaint_terminal *term, int s)

  This is a wrapper for using :c:func:`termpaint_input_expect_legacy_mouse_reports` with a terminal object.

  When mouse reporting is enabled this function is internally called with
  :c:macro:`TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE`, so it should be rarely needed to call this explicitly.

.. c:function:: void termpaint_terminal_glitch_on_out_of_memory(termpaint_terminal *term)

  Normally termpaint aborts the process on memory allocation failure to avoid hard to debug glitches.

  When this function is called instead termpaint tries to continue, but potentially discarding output
  characters and attributes where allocation would be needed.

  Call this function if your application needs to be resilient against memory allocation failures.

  To use termpaint in such environments it's additionally required to call variants of functions ending
  in _or_nullptr or _mustcheck instead of the base variant whenever those exist in the header file.

  See :ref:`malloc-failure` for details.


Functions for integrations
--------------------------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: const char *termpaint_terminal_restore_sequence(const termpaint_terminal *term)

  Returns a null terminated string that can be used to restore the terminal to it's base state.

  The restore string is the same string that is used when calling
  :c:func:`termpaint_terminal_free_with_restore` or :c:func:`termpaint_terminal_pause`.

.. c:function:: void termpaint_terminal_callback(termpaint_terminal *term)

  If the application has set ``request_callback`` in the integration structure, this function needs to be called after
  a delay when the terminal object requests it by invoking the ``request_callback`` callback.

.. c:function:: void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length)

  The integration part of the application has to call this function to pass terminal input data to the terminal object.
  See :doc:`integration` for details.

  The application has to ensure that this function is never called recursively from a callback
  with any :c:type:`termpaint_input` object that is already in a call to ``termpaint_terminal_add_input_data``.

  This is a wrapper for using :c:func:`termpaint_input_add_data` with a terminal object.

