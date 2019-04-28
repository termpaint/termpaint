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
value that can be used instead of string comparision.

In addition to the real keys there is a pseudo key "i_resync" which is used internally and should usually be ignored by
the application.

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

  The key was pressed while the shift alg gr key (alternate graphics) was held.

Event Types
-----------

.. c:macro:: TERMPAINT_EV_UNKNOWN

  An unknown event was send by the terminal.

.. c:macro:: TERMPAINT_EV_CHAR

  A :ref:`character event <character event>` was send by the terminal.

.. c:macro:: TERMPAINT_EV_KEY

  A :ref:`key event <key event>`  was send by the terminal.

.. c:macro:: TERMPAINT_EV_AUTO_DETECT_FINISHED

  The auto detection phase was finished. The application can now create it's user interface.
  See TODO

.. c:macro:: TERMPAINT_EV_OVERFLOW

  The internal parsing buffer was discarded because a sequence was to long to fit.

.. c:macro:: TERMPAINT_EV_INVALID_UTF8

  The terminal sent invalid utf8 encoded data.

.. c:macro:: TERMPAINT_EV_CURSOR_POSITION

  The terminal sent a cursor position report.

.. c:macro:: TERMPAINT_EV_MODE_REPORT

  The terminal sent a mode report.

.. c:macro:: TERMPAINT_EV_COLOR_SLOT_REPORT

  The terminal sent a color report.

.. c:macro:: TERMPAINT_EV_REPAINT_REQUESTED

  Termpaint aquired additional data and a repaint could improve the rendering of the user interface.

.. c:macro:: TERMPAINT_EV_RAW_PRI_DEV_ATTRIB

  The terminal send a primary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_SEC_DEV_ATTRIB

  The terminal send a secondary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_3RD_DEV_ATTRIB

  The terminal send a tertiary device attributes report.

.. c:macro:: TERMPAINT_EV_RAW_DECREQTPARM

  The terminal send a dec terminal parameters report.

The event structure
-------------------

.. c:type:: termpaint_event

::

    int type;

The type of the events. Depending on the value of ``type`` different parts of the event contain valid data.

if ``type`` is :c:macro:`TERMPAINT_EV_CHAR` or :c:macro:`TERMPAINT_EV_INVALID_UTF8`:

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

If ``type`` is TERMPAINT_EV_KEY:

  ::

      struct {
          unsigned length;
          const char *atom;
          int modifier;
      } key;

  ``atom`` and ``length`` together describe a (non null terminated) key from the table :doc:`keys` pressed. Alternativly
  ``atom`` can directly compared to the one of the pointers returned by one of the termpaint_input_* functions.
  ``modifiers`` describes the :ref:`modifiers` held.

if ``type`` is :c:macro:`TERMPAINT_EV_CURSOR_POSITION`:

  ::

      struct {
          int x;
          int y;
          _Bool safe;
      } cursor_position;

  A cursor position report. ``x`` and ``y`` contain the cell coordinates of the reported cursor position.

  If ``safe`` is true the cursor position report was in a format that is not ambiguous with a keyboard event.
  See :c:func:`termpaint_input_expect_cursor_position_report` for handling of ambiguous cursor position events.

If ``event`` is :c:macro:`TERMPAINT_EV_MODE_REPORT`

  ::

      struct {
          int number;
          int kind;
          int status;
      } mode;

  TODO

If ``event`` is :c:macro:`TERMPAINT_EV_RAW_PRI_DEV_ATTRIB`, :c:macro:`TERMPAINT_EV_RAW_SEC_DEV_ATTRIB`,
:c:macro:`TERMPAINT_EV_RAW_3RD_DEV_ATTRIB` or :c:macro:`TERMPAINT_EV_RAW_DECREQTPARM`:

  ::

      struct {
          unsigned length;
          const char *string;
      } raw;

  TODO

if ``event`` :c:macro:`TERMPAINT_EV_COLOR_SLOT_REPORT`:

  ::

      struct {
          int slot;
          const char *color;
          unsigned length;
      } color_slot_report;

  TODO
