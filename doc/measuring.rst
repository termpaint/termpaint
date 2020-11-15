Measuring Text
===============

.. c:type:: termpaint_text_measurement

The text measurement functions are offered in variants for utf8, utf16 and utf32 to allow for flexibility in the storage
format applications use for bulk data storage. All functions should behave identical regardless of utf-variant when the
results are used according to the relevant code unit size.

Text is printed on a grid of cells. Letters like ``a`` use one cell each. But not every unicode codepoint uses one cell.
There are wide characters and characters that modify the preceding character. This text measuring api is meant to be
able to cover these special cases.

Currently it supports single cell characters, double cell characters (like Japanese characters and some emoji) and
zero width combining characters.

Logically text strings are separated into clusters. Each cluster is a unit that is displayed as one unbreakable symbol.
A cluster has a width in cells, a width in codepoints and a width in code units (bytes for utf-8, 16bit words for utf-16).

A simple example to measure the first cluster in a utf-16 string looks like this ::

  termpaint_text_measurement *tm = termpaint_text_measurement_new(surface);
  termpaint_text_measurement_set_limit_clusters(tm, 1);
  termpaint_text_measurement_feed_utf16(tm, data, size, true);
  int codePoints = termpaint_text_measurement_last_codepoints(tm);
  int codeUnits = termpaint_text_measurement_last_ref(tm);
  int columns = termpaint_text_measurement_last_width(tm);
  termpaint_text_measurement_free(tm);

The termpaint_text_measurement_set_limit\_\* function allow to set how much of the string is measured in one call to
:c:func:`termpaint_text_measurement_feed_utf16()`. The feed function returns true if one of the set limits was reached.

When no limit is set the feed function will always return false. If more than one limit is set, the limit that is
reached first is significant.

After :c:func:`termpaint_text_measurement_feed_utf16()` returns true, the termpaint_text_measurement_last\_\* family of
functions can be used to check the measurements up to and including the last cluster that did not exceed any of the limits.

The :c:func:`termpaint_text_measurement_feed_utf16()` function is designed to allow measuring text that is stored in
multiple separate segments in memory (like in a rope or a linked list of substrings). If it returns false the application
can call it again with data from the next segment. All calls except for the call for the last segment have to be made
with the ``final`` parameter set to false.

To ensure the last cluster in a string is properly measured, the last (or only – when not using segments) call to
:c:func:`termpaint_text_measurement_feed_utf16()` has to be made with the ``final`` parameter set to true. This is
because the last cluster could still be extended by additional non spacing marks in the next segment of a string if it
would not have been the last segment.

After a limit is reached the measurement can be continued by setting a new limit and resuming with calling
:c:func:`termpaint_text_measurement_feed_utf16()` starting from the next code unit after cluster where the limit was
reached. In practice this mean starting from the ``termpaint_text_measurement_last_ref(tm)``-th code unit in the
original string. (When using segments this count includes code units from the previous segments. It is possible that
resuming a measurement needs restarting in a previous segment)

The :c:func:`termpaint_text_measurement_feed_utf8` and :c:func:`termpaint_text_measurement_feed_utf32` functions
work using the same principle, just with a different encoding for input and different code units sizes for
:c:func:`termpaint_text_measurement_last_ref`.

Mixing different variants in one measurement is possible as
long as switching between utf-variants is done only at codepoint boundaries, but should still be avoided because it makes
interpreting the results very hard.

:c:func:`termpaint_text_measurement_feed_codepoint` allows feeding in codepoints from other storage formats one by one.
It works similar, but with freely definable meaning of what exactly :c:func:`termpaint_text_measurement_last_ref` means,
as the increments for each codepoint is supplied by the user.

Functions
---------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: termpaint_text_measurement* termpaint_text_measurement_new(termpaint_surface *surface)

  Create a new text measurement object.

  The application has to free this with :c:func:`termpaint_text_measurement_free()`.

  The lifetime of this object must not exceed the lifetime of the terminal object
  originating the passed surface.

.. c:function:: void termpaint_text_measurement_free(termpaint_text_measurement *m)

  Frees the text measurement object.

.. c:function:: void termpaint_text_measurement_reset(termpaint_text_measurement *m)

  Resets the text measurement object to enable use for a fresh measurement.

  It removes all limits and resets the state back to zero clusters, columns, codepoints and code units.

.. c:function:: int termpaint_text_measurement_last_codepoints(termpaint_text_measurement *m)

  Returns the number of code points up to and including the last measured cluster not exceeding any set limits.

.. c:function:: int termpaint_text_measurement_last_clusters(termpaint_text_measurement *m)

  Returns the number of clusters up to and including the last measured cluster not exceeding any set limits.

.. c:function:: int termpaint_text_measurement_last_width(termpaint_text_measurement *m)

  Returns the width in cells of the text up to and including the last measured cluster not exceeding any set limits.

.. c:function:: int termpaint_text_measurement_last_ref(termpaint_text_measurement *m)

  If using :c:func:`termpaint_text_measurement_feed_utf8()`:
    Returns the number of bytes up to and including the last measured cluster not exceeding any set limits.

  If using :c:func:`termpaint_text_measurement_feed_utf16()`:
    Returns the number of utf16 code units up to and including the last measured cluster not exceeding any set limits.

  If using :c:func:`termpaint_text_measurement_feed_utf32()`:
    Returns the same as :c:func:`termpaint_text_measurement_last_codepoints()`

  If using :c:func:`termpaint_text_measurement_feed_codepoint()`:
    Returns the sum of all ``ref_adjust`` values up to and including the last measured cluster not exceeding any set limits.

.. c:function:: int termpaint_text_measurement_pending_ref(termpaint_text_measurement *m)

  Like :c:func:`termpaint_text_measurement_last_ref` but also include code units that belong the cluster currently in
  processing (if any).

.. c:function:: void termpaint_text_measurement_set_limit_codepoints(termpaint_text_measurement *m, int new_value)

  Sets the limit for codepoints. -1 means no limit. The limit must be greater than the current position.

.. c:function:: int termpaint_text_measurement_limit_codepoints(termpaint_text_measurement *m)

  Returns the value set using :c:func:`termpaint_text_measurement_set_limit_codepoints()`.

.. c:function:: void termpaint_text_measurement_set_limit_clusters(termpaint_text_measurement *m, int new_value)

  Sets the limit for clusters. -1 means no limit. The limit must be greater than the current position.

.. c:function:: int termpaint_text_measurement_limit_clusters(termpaint_text_measurement *m)

  Returns the value set using :c:func:`termpaint_text_measurement_set_limit_clusters()`.

.. c:function:: void termpaint_text_measurement_set_limit_width(termpaint_text_measurement *m, int new_value)

  Sets the limit for the width. -1 means no limit. The limit must be greater than the current position.

.. c:function:: int termpaint_text_measurement_limit_width(termpaint_text_measurement *m)

  Returns the value set using :c:func:`termpaint_text_measurement_set_limit_width()`.

.. c:function:: void termpaint_text_measurement_set_limit_ref(termpaint_text_measurement *m, int new_value)

  Sets the limit for reference. Depending on the feed function used the reference is in utf32, utf16 or utf8 code units.
  -1 means no limit. The limit must be greater than the current position.

.. c:function:: int termpaint_text_measurement_limit_ref(termpaint_text_measurement *m)

  Returns the value set using :c:func:`termpaint_text_measurement_set_limit_ref()`.

.. c:function:: _Bool termpaint_text_measurement_feed_utf8(termpaint_text_measurement *m, const char *code_units, int length, _Bool final)

  Add the utf8 encoded string starting at ``code_units`` with length ``length`` to the measurement. Set ``final`` to
  true if this is the last segment of the to be measured string.

  See termpaint_text_measurement_last\_\* for functions to retrieve the measurement results.

  If no limits are set, it always returns false.

  Otherwise returns ``false`` if no limit was reached. Returns true if the limit was reached while measuring.

.. c:function:: _Bool termpaint_text_measurement_feed_utf16(termpaint_text_measurement *m, const uint16_t *code_units, int length, _Bool final)

  Add the utf16 encoded string (in host endianness) starting at ``code_units`` with length ``length`` to the measurement.
  Set ``final`` to true if this is the last segment of the to be measured string.

  See termpaint_text_measurement_last\_\* for functions to retrieve the measurement results.

  If no limits are set, it always returns false.

  Otherwise returns ``false`` if no limit was reached. Returns true if the limit was reached while measuring.

.. c:function:: _Bool termpaint_text_measurement_feed_utf32(termpaint_text_measurement *m, const uint32_t *chars, int length, _Bool final)

  Add the utf32 encoded string (in host endianness) starting at ``chars`` with length ``length`` to the measurement.
  Set ``final`` to true if this is the last segment of the to be measured string.

  See termpaint_text_measurement_last\_\* for functions to retrieve the measurement results.

  If no limits are set, it always returns false.

  Otherwise returns ``false`` if no limit was reached. Returns true if the limit was reached while measuring.

.. container:: hidden-references

  .. c:macro:: TERMPAINT_MEASURE_NEW_CLUSTER
  .. c:macro:: TERMPAINT_MEASURE_LIMIT_REACHED

.. c:function:: int termpaint_text_measurement_feed_codepoint(termpaint_text_measurement *m, int ch, int ref_adjust)

  This is a low level function used to implement the termpaint_text_measurement_feed_utf* family of functions. It adds
  a single codepoint to the measurement and returns a bit flag with the result.

  if ``TERMPAINT_MEASURE_NEW_CLUSTER`` is set:
    The added code point started a new cluster. The information about the previous cluster is now available using the
    termpaint_text_measurement_last_* functions.

  if ``TERMPAINT_MEASURE_LIMIT_REACHED`` is set:
    The limit was reached while measuring. See termpaint_text_measurement_last_* for function to retrieve the
    measurement results. To continue measuring the measurement needs to be restarted at the point where the limit was
    reached.
