termpaint_input
===============

.. c:type:: termpaint_input

``termpaint_input`` is a sublibrary for terminal input handling. It can be used standalone, but when using termpaint for
terminal output it's always used as an internal dependency.

Thus while standalone usage is fully supported, most applications will not use it explicitly. Where interaction with the
input handling is needed there are wrappers that take a :c:type:`termpaint_terminal` object and forward settings to the
internal instance.

.. TODO general API design

Input bytes from the terminal to termpaint_input need to be passed to :c:func:`termpaint_input_add_data`. If enough
bytes have accumulated to identify a input sequence termpaint_input will call the event callback set by the application
using :c:func:`termpaint_input_set_event_cb` with the interpreted :doc:`event <events>`.



Functions
---------

These functions are contained in the header ``termpaintx_input.h``

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: termpaint_input *termpaint_input_new(void)

  Create a new termpaint_input object.

  The application has to free this with :c:func:`termpaint_input_free`

.. c:function:: void termpaint_input_free(termpaint_input *ctx)

  Frees the termpaint_input object ``ctx``.

.. c:function:: void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *user_data, termpaint_event* event), void *user_data)

  The application must use this function to set an event callback. See :doc:`events` for details about events produced
  by terminal input. The input is buffered and as soon as a complete sequence is detected the raw filter callback is
  invoked and if that callback didn't suppress further processing an an event is generated and passed to the event
  callback.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_set_event_cb`

.. c:function:: void termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length)

  This is the function to feed incoming data from the terminal to the input processing.

  The application has to ensure that this function is never called recursively from a callback
  with any :c:type:`termpaint_input` object that is already in a call to ``termpaint_input_add_data``.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_add_input_data`

.. c:function:: void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data)

  This function allows settings a callback that is called with raw sequences before interpretation. The application can
  inspect the sequence in this callback. If the callback returns true the sequence is not interpreted further.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_set_raw_input_filter_cb`

.. c:function:: void termpaint_input_expect_cursor_position_report(termpaint_input *ctx)

  Needs to be called for each ``ESC[6n`` sequence send to the terminal to ensure the result is
  interpreted as cursor position report instead of a key press.

  This should not be called if ``ESC[?6n`` is used and properly supported by the terminal.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_expect_cursor_position_report`

.. c:function:: void termpaint_input_expect_legacy_mouse_reports(termpaint_input *ctx, int s)

  Legacy mouse modes use sequences that do not fit into the ECMA-48 sequence schema. They are composed of ``ESC[M``
  followed by additional data. The original mouse reporting modes use this kind of encoding. An additional mode was
  defined later that can not reliably differentiated thus apart from "off" there are two choices.

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_INPUT_EXPECT_NO_LEGACY_MOUSE

      Disable parsing of legacy mouse sequences.

    .. c:macro:: TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE

      Expect legacy mouse sequences in the original format composed of 3 bytes following the ``ESC[M`` sequence.

    .. c:macro:: TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE_MODE_1005

      Expect legacy mouse sequences in the multibyte format similar to 2 byte utf-8 encoding. The ``ESC[M`` sequence is
      followed by 3 of these variable length representations. This encoding is commonly selected using `ESC[?1005h`.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_expect_legacy_mouse_reports`

.. c:function:: void termpaint_input_handle_paste(termpaint_input *ctx, _Bool enable)

  Set whether bracketed paste terminal sequences are parsed as a :c:macro:`TERMPAINT_EV_PASTE`
  event (if ``enable`` is true) or if the contents of the paste is parsed like normal input,
  sandwiched in :c:macro:`TERMPAINT_EV_MISC` events with atoms :c:func:`termpaint_input_paste_begin`
  and :c:func:`termpaint_input_paste_end`.

  Explicit paste handling is an switchable terminal feature, this setting only is meaningful
  if bracketed paste has been enabled in the terminal.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_handle_paste`

.. c:function:: void termpaint_input_expect_apc_sequences(termpaint_input *ctx, _Bool enable)

  Consider input starting with ``ESC_`` as a sequence to be terminated by a string terminator (ST) instead of as
  alt ``_``. The resync trick will still detect ``ESC_`` if it's followed by ``ESC[0n``.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_expect_apc_input_sequences`

.. c:function:: const char* termpaint_input_peek_buffer(const termpaint_input *ctx)

  This function in conjunction with :c:func:`termpaint_input_peek_buffer_length` allows an application
  to observe input data that is buffered by not yet processed. If called after :c:func:`termpaint_input_add_data`
  returned, this will contain data in partial or ambiguous sequences not yet processed.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_peek_input_buffer`

.. c:function:: int termpaint_input_peek_buffer_length(const termpaint_input *ctx)

  Returns the length of the valid data for :c:func:`termpaint_input_peek_buffer`.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_peek_input_buffer_length`

.. c:function:: void termpaint_input_activate_quirk(termpaint_input *ctx, int quirk)

  Most input parsing is independent of the connected terminal. But some sequences are used by different terminals for
  different functions. In that case quirks allow fine tuning the input parsing to pick the correct interpretation.

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_INPUT_QUIRK_BACKSPACE_X08_AND_X7F_SWAPPED

      Without this quirk char 0x7f (DEL) is interpreted as backspace key and char 0x08 (ASCII BS) is interpreted as
      ctrl backspace.

      With this quirk activated the interpretation is swapped.

    .. c:macro:: TERMPAINT_INPUT_QUIRK_C1_FOR_CTRL_SHIFT

      By default C1 control characters are not interpreted as special key input. When this quirk is activated char
      0x80 is interpreted as ctrl shift space and chars 0x81 to 0x9a are interpreted as ctrl shift A-Z.

  The wrapper for using this with a terminal object is :c:func:`termpaint_terminal_activate_input_quirk`
