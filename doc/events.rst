Events
======

Terminal input is translated to events. The application handles the events by implementing a event callback and setting
it on the terminal object by using the :c:func:`termpaint_terminal_set_event_cb()` function.

The primary input events are character events, key events and mouse events.

Each event has a ``type`` which describes what kind of event is emitted. An application should ignore events of types
that the application does not know how to handle, as from time to time new events will be added.


.. _character event:

Character events
----------------

Character events are emitted when the user presses a key that has a printable character associated. The ``type`` field
will be set to :c:macro:`TERMPAINT_EV_CHAR`. The input character is described by an utf8 string (not null terminated)
that is available as ``c.string`` with length ``c.length``. Additionally the field ``c.modifier`` contains a bit field
describing the modifier keys held while the character was input.

The space key and enter key are handled as key events as described in the following section.

.. _key event:

Key events
----------

Key events are emitted for key presses on non character keys like space, enter, the edit keys and function keys. The
``type`` field will be set to :c:macro:`TERMPAINT_EV_KEY`. The key is described by the ``key.atom`` field which can be
used in two ways. Together with ``key.length`` it contains a (not null terminated) string naming the key. Additionally
``key.atom`` can be compared as pointer to the result of the termpaint_input_* functions that return a unique pointer
value that can be used instead of string comparison.

See :doc:`keys` for a complete list of keys.

Additionally the field ``c.modifier`` contains a bit field
describing the modifier keys held while the character was input.


.. _modifiers:

Modifiers
---------

Modifiers is a bit flag of combinations of the following:

.. c:macro:: TERMPAINT_MOD_SHIFT

  The key was pressed while the shift key was held.

.. c:macro:: TERMPAINT_MOD_CTRL

  The key was pressed while the ctrl key was held.

.. c:macro:: TERMPAINT_MOD_ALT

  The key was pressed while the alt key was held.

.. c:macro:: TERMPAINT_MOD_ALTGR

  The key was pressed while the shift alt-gr key (alternate graphics) was held.

.. _mouse event:

Not all terminals support reporting all modifiers with each key. Generally the function keys
and arrow keys have better modifier reporting that the printable characters.

Mouse events
------------

Many terminals have optional mouse reporting modes. If these are supported and activated by calling
:c:func:`termpaint_terminal_set_mouse_mode` input events for mouse clicks and possibly more events will
be produced.

The ``type`` field will be set to :c:macro:`TERMPAINT_EV_KEY`.

Mouse events consist of ``x`` and ``y`` coordinates of the event. ``modifier`` held while the event was
generated and an ``action`` that describes if the event was a click, release or a movement event.
For click events the ``button`` number is available.

.. _misc-events:

Misc events
-----------

Various non keyboard related terminal events that don't need additional data.

.. c:function:: const char *termpaint_input_focus_in(void)

  The terminal has received focus. Terminal support varies and is opt-in. Focus
  change events can be requested using :c:func:`termpaint_terminal_request_focus_change_reports`.

  String value: ``FocusIn``

.. c:function:: const char *termpaint_input_focus_out(void)

  The terminal has lost focus. Terminal support varies and is opt-in. Focus
  change events can be requested using :c:func:`termpaint_terminal_request_focus_change_reports`.

  String value: ``FocusOut``

.. c:function:: const char *termpaint_input_paste_begin(void)

  If enabled in the terminal pasted text is bracketed by paste begin and paste end markers.
  If translation of the whole sequence to paste events is disabled using :c:func:`termpaint_input_handle_paste`
  then this misc event is emitted on paste start.

  Terminal support varies and is opt-in.

  String value: ``PasteBegin``

.. c:function:: const char *termpaint_input_paste_end(void)

  If enabled in the terminal pasted text is bracketed by paste begin and paste end markers.
  If translation of the whole sequence to paste events is disabled using :c:func:`termpaint_input_handle_paste`
  then this misc event is emitted on paste end.

  Terminal support varies and is opt-in.

  String value: ``PasteEnd``

.. c:function:: const char *termpaint_input_i_resync(void)

  This misc event is emitted when the input parser was resynchronized by
  requesting a device status report due to a incomplete terminal sequence.

  See :ref:`resync` for details.

  String value: ``i_resync``

Main event types
----------------

.. c:macro:: TERMPAINT_EV_CHAR

  A :ref:`character event <character event>` was sent by the terminal.

.. c:macro:: TERMPAINT_EV_KEY

  A :ref:`key event <key event>` was sent by the terminal.

.. c:macro:: TERMPAINT_EV_PASTE

  The terminal sent a clipboard paste event.

.. c:macro:: TERMPAINT_EV_AUTO_DETECT_FINISHED

  The auto detection phase was finished. The application can now create it's user interface.
  See :ref:`terminal-setup`.

  The event does not contain additional data.

.. c:macro:: TERMPAINT_EV_MOUSE

  A :ref:`mouse event<mouse event>` was sent by the terminal.

.. c:macro:: TERMPAINT_EV_MISC

  Other simple terminal events.

.. c:macro:: TERMPAINT_EV_REPAINT_REQUESTED

  Termpaint acquired additional data and a repaint could improve the rendering of the user interface. Currently used when
  after :c:func:`termpaint_terminal_set_color` has made sure the color can be restored to it's original value on restore.

  The event does not contain additional data.

Other event types
-------------------

.. c:macro:: TERMPAINT_EV_UNKNOWN

  An unknown event was sent by the terminal.

  The event does not contain additional data.

.. c:macro:: TERMPAINT_EV_OVERFLOW

  The internal parsing buffer was discarded because a sequence was to long to fit.

  The event does not contain additional data.

.. c:macro:: TERMPAINT_EV_INVALID_UTF8

  The terminal sent invalid utf8 encoded data.

.. c:macro:: TERMPAINT_EV_CURSOR_POSITION

  The terminal sent a cursor position report.

.. c:macro:: TERMPAINT_EV_MODE_REPORT

  The terminal sent a mode report.

.. c:macro:: TERMPAINT_EV_COLOR_SLOT_REPORT

  The terminal sent a color report.

.. c:macro:: TERMPAINT_EV_RAW_PRI_DEV_ATTRIB

  The terminal sent a primary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_SEC_DEV_ATTRIB

  The terminal sent a secondary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_3RD_DEV_ATTRIB

  The terminal sent a tertiary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_DECREQTPARM

  The terminal sent a dec terminal parameters report.

.. c:macro:: TERMPAINT_EV_PALETTE_COLOR_REPORT

  The terminal sent a palette color report.

.. c:macro:: TERMPAINT_EV_RAW_TERMINFO_QUERY_REPLY

  The terminal sent a report for a ``ESCP+????ESC\\`` query.

.. c:macro:: TERMPAINT_EV_RAW_TERM_NAME

  The terminal sent a report for a ``ESC[>q`` query.

Termpaint only uses event types >= 0. If the design of an application needs
application internal codes that coexist with termpaint event types in the same
``int``-typed variable, it may use negative numbers for these.

The event structure
-------------------

.. c:type:: termpaint_event

::

    int type;

The type of the events. Depending on the value of ``type`` different parts of the event contain valid data.

If ``type`` is :c:macro:`TERMPAINT_EV_CHAR` or :c:macro:`TERMPAINT_EV_INVALID_UTF8`:

  ::

      struct {
          unsigned length;
          const char *string;
          int modifier;
      } c;

  ``string`` and ``length`` together describe a (non null terminated) string with the raw data from the terminal.
  ``modifiers`` describes the :ref:`modifiers` held.

  If ``type`` is :c:macro:`TERMPAINT_EV_CHAR` this describes a key press. If ``type`` is
  :c:macro:`TERMPAINT_EV_INVALID_UTF8` the terminal sent a invalidly encoded utf8 sequence.

If ``type`` is :c:macro:`TERMPAINT_EV_KEY`:

  ::

      struct {
          unsigned length;
          const char *atom;
          int modifier;
      } key;

  ``atom`` and ``length`` together describe which (non null terminated) key from the table :doc:`keys` was pressed.
  Alternatively ``atom`` can directly compared to the one of the pointers returned by one of the termpaint_input_*
  functions. ``modifiers`` describes the :ref:`modifiers` held.

If ``type`` :c:macro:`TERMPAINT_EV_MOUSE`:

  ::

      struct {
          int x;
          int y;
          int raw_btn_and_flags;
          int action;
          int button; // button == 3 means release with unknown button
          int modifier;
      } mouse;

  Each mouse event has a position described by ``x``, ``y`` and the state of the keyboard :ref:`modifiers`
  held is made available in ``modifier``.

  ``action`` describes if the event was a button press, button release or mouse pointer move event.

  Which of these events are sent depends on the mouse mode of the terminals. See :c:func:`termpaint_terminal_set_mouse_mode`
  for details.

  For mouse press events the number of the pressed button is available as ``button``. Mouse release and move events
  have limited support for this information depending on the terminal. Left, middle and right buttons have the numbers
  0, 1 and 2 respectively. Button number 3 is used by terminals when no button is held or the button information is not
  available.

    .. c:macro:: TERMPAINT_MOUSE_PRESS

      The button ``button`` was pressed.

    .. c:macro:: TERMPAINT_MOUSE_RELEASE

      A mouse button was released. Depending on the terminal ``button`` is either 3 or the number of the
      just release mouse button.

    .. c:macro:: TERMPAINT_MOUSE_MOVE

      The mouse cursor was moved.

  ``raw_btn_and_flags`` contains a raw and undecoded value from the terminal that contains information from which
  ``modifiers`` and ``button`` was interpreted. It's available for not yet fully supported extended event information
  from the terminal.

If ``type`` is :c:macro:`TERMPAINT_EV_PASTE`:

  ::

      struct {
          unsigned length;
          const char *string;
          _Bool initial;
          _Bool final;
      } paste;

  A paste event. As termpaint does not buffer the pasted characters a paste from the terminal generates many fragmented
  events. ``initial`` is true if this event belongs to the start of a paste operation and ``final`` is true if this
  event marks the end of the paste operation.

  ``string`` and ``length`` together describe a (non null terminated) string with a fragment of the pasted characters.

  The application should be prepared to get empty fragments and fragments with one or multiple characters. Details how
  the events are fragmented are subject to change in future versions of the library.

If ``type`` is :c:macro:`TERMPAINT_EV_MISC`:

  ::

      struct {
          unsigned length;
          const char *atom;
      } misc;

  A misc event. Available value for atom are described in `Misc Events`_.

  ``atom`` can directly compared to the one of the pointers returned by one of the functions described there.

  Alternatively ``atom`` and ``length`` together form a (non null terminated) string that can compared to one of
  the strings also described in that section.

If ``type`` is :c:macro:`TERMPAINT_EV_CURSOR_POSITION`:

  ::

      struct {
          int x;
          int y;
          _Bool safe;
      } cursor_position;

  A cursor position report. ``x`` and ``y`` contain the cell coordinates of the reported cursor position.

  If ``safe`` is true the cursor position report was in a format that is not ambiguous with a keyboard event.
  See :c:func:`termpaint_input_expect_cursor_position_report` for handling of ambiguous cursor position events.

If ``type`` is :c:macro:`TERMPAINT_EV_MODE_REPORT`

  ::

      struct {
          int number;
          int kind;
          int status;
      } mode;

  The terminal send a report for a terminal mode query. If ``kind`` is 1 the report is for a "private" mode, otherwise
  it's for a non private mode. ``number`` specifies the mode number. ``status`` is the status of the mode and contains
  a value from the following list:

  0

    unknown mode

  3

    mode is set

  4

    mode is reset

If ``event`` is :c:macro:`TERMPAINT_EV_RAW_PRI_DEV_ATTRIB`, :c:macro:`TERMPAINT_EV_RAW_SEC_DEV_ATTRIB`,
:c:macro:`TERMPAINT_EV_RAW_3RD_DEV_ATTRIB`, :c:macro:`TERMPAINT_EV_RAW_DECREQTPARM`,
:c:macro:`TERMPAINT_EV_RAW_TERMINFO_QUERY_REPLY` or :c:macro:`TERMPAINT_EV_RAW_TERM_NAME`:

  ::

      struct {
          unsigned length;
          const char *string;
      } raw;

  The mostly raw report from the terminal is contained in ``string`` and ``length`` which together describe
  a (non null terminated) string.

If ``type`` is :c:macro:`TERMPAINT_EV_COLOR_SLOT_REPORT`:

  ::

      struct {
          int slot;
          const char *color;
          unsigned length;
      } color_slot_report;

  A report for a query of a global terminal color was received. ``slot`` contains the number of the slot and
  ``color`` and ``length`` together describe a (non null terminated) string which contains the color data reported
  by the terminal.

If ``type`` is :c:macro:`TERMPAINT_EV_PALETTE_COLOR_REPORT`:

  ::

      struct {
          int color_index;
          const char *color_desc;
          unsigned length;
      } palette_color_report;

  A report for a query of a palette terminal color was received. ``color_index`` contains index of the color in the
  palette and ``color`` and ``length`` together describe a (non null terminated) string which contains the color
  data reported by the terminal.
