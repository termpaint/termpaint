Surface
=======

.. c:type:: termpaint_surface

Surfaces are the abstraction for output into the terminal. Each :doc:`terminal object<terminal>` has a primary surface
that is used for output. A pointer to the primary surface can be obtained by the :c:func:`termpaint_terminal_get_surface()`
function and output can be triggered by the :c:func:`termpaint_terminal_flush()` function. Additionally there can be
any number of offscreen surfaces for a terminal. These can be created by :c:func:`termpaint_terminal_new_surface()`.
The offscreen surfaces can by copied to other surfaces using the :c:func:`termpaint_surface_copy_rect()` function to
ultimately either copy to the primary surface or to save the contents to external storage.

Output to a surface is done using the :c:func:`termpaint_surface_write_*<termpaint_surface_write_with_attr()>` family of
functions for output of text or symbols and the
:c:func:`termpaint_surface_clear_rect*<termpaint_surface_clear_rect_with_attr()>` family of functions for clearing areas
of the surface.

The surface is a rectangle of cells. The cells each have the same size. The smallest unit of text that can be rendered
is a cluster that represents a "character" and covers one cell or two cells next to each other on a line. Each cluster
additionally has a foreground color, a background color, a decoration color and a combination of styles.

.. note:: In many terminal the foreground color and some attributes are even important for blank cells. For example
   text selection might not display properly if foreground and background color are the same. For best compatibility
   always use a combination of foreground and background color that has decent contrast.

Blank space at the end of a line is handled specially in many terminals. For example, for the terminal native text
selection, a range of cells at the end of the line can be ignored for text selection's contents if they are detected as
insignificant. Termpaint allows marking cells as erased to mirror this behavior. All cells at the very end of a line
that are marked as erased are send to the terminals as insignificant, if supported by the terminal.

.. container:: hidden-references

  .. c:macro:: TERMPAINT_ERASED

When using one of the clear functions or writing ``TERMPAINT_ERASED`` characters in a surface these cells will be
counted as erased and not be considered significant spaces at the end of a line in supported terminals.

Terminals commonly group lines into paragraphs to determine if text that is copied with the terminal native
clipboard support should contain line breaks between lines of the terminal display. For compatible terminals
termpaint can connect subsequent lines into a paragraph using soft wrap markers. If a line ends in a cell with
a soft wrap marker and the following line begins with a cell with a soft wrap marker termpaint will connect both lines
into a paragraph. Applications can set those using :c:func:`termpaint_surface_set_softwrap_marker` after writing
text to the cell.

Many surface function take an pointer to an :doc:`attribute object<attributes>` to specify the colors and styles to be
used for the modified cells.

Furthermore there are functions to read back the current contents of a surface (see the
:c:func:`termpaint_surface_peek_*<termpaint_surface_peek_text()>` family of functions) and to compare the contents of
two surfaces (:c:func:`termpaint_surface_same_contents()`) as well are changing the color of the cells on
a surface (:c:func:`termpaint_surface_tint()`).

Functions
---------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: termpaint_surface *termpaint_terminal_new_surface(termpaint_terminal *term, int width, int height)

  Creates an new off-screen surface for usage with terminal object ``term``. The new surface has the size ``width``
  columns by ``height`` lines and is initialized with spaces with default attributes (TERMPAINT_DEFAULT_COLOR for all
  colors).

  The application has to free this with :c:func:`termpaint_surface_free`.

  The lifetime of this object must not exceed the lifetime of the passed terminal object.

.. c:function:: termpaint_surface *termpaint_surface_new_surface(termpaint_surface *surface, int width, int height)

  Like :c:func:`termpaint_terminal_new_surface` but instead of directly requiring a terminal object uses a existing
  surface for that terminal object.

  The lifetime of this object must not exceed the lifetime of the terminal object originating the passed surface.

.. c:function:: termpaint_surface *termpaint_surface_duplicate(termpaint_surface *surface)

  Creates an new off-screen surface for usage with terminal object for which the source surface ``surface``
  was created. The new surface has the same size as the source surface ``surface`` and is initialized with
  a copy of the source surface ``surface`` content.

  The application has to free this with :c:func:`termpaint_surface_free`.

.. c:function:: void termpaint_surface_free(termpaint_surface *surface)

  Frees a surface allocated with :c:func:`termpaint_terminal_new_surface`. This must not be called on the primary
  surface of a terminal object, because that is owned by the terminal object.

.. c:function:: void termpaint_surface_resize(termpaint_surface *surface, int width, int height)

  Change the size of a surface to ``width`` columns by ``height`` lines. The current contents is erased as if the
  surface had been freshly created by :c:func:`termpaint_terminal_new_surface`.

.. c:function:: int termpaint_surface_width(const termpaint_surface *surface)

  Returns the current width of the surface.

.. c:function:: int termpaint_surface_height(const termpaint_surface *surface)

  Returns the current height of the surface.

.. c:function:: void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr)

  Write a text given in the null terminated utf8 string ``string`` to the surface starting in cell ``x``, ``y``.
  Uses ``attr`` as attributes for all newly placed characters.

  The length of the run of cells where the characters will be placed can be calculated using the
  :doc:`string measurement functions<measuring>`.

  If any modified cells have previously been part of a multicell character cluster the cluster as a whole is erased.
  Cells not overwritten will keep their previous attributes (colors, etc).

  If ``string`` contains the special character :c:macro:`TERMPAINT_ERASED` the corresponding cells are marked as erased.

.. c:function:: void termpaint_surface_write_with_len_attr(termpaint_surface *surface, int x, int y, const char *string, int len, const termpaint_attr *attr)

  Like :c:func:`termpaint_surface_write_with_attr()` but does take a explicit length parameter instead of
  writing the string until it encounters a NUL character in the string.

.. c:function:: void termpaint_surface_write_with_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr, int clip_x0, int clip_x1)

  Like :c:func:`termpaint_surface_write_with_attr()` but additionally applies clipping so that only cells in column
  ``clip_x0`` (inclusive) to column ``clip_x1`` are used for placing characters. ``x`` may be less than ``clip_x0``,
  in that case characters at the start of the string are not placed as needed to maintain the clipping interval.

  The clip range does *not* prevent modifications of characters outside of the interval to be changed if clusters cross
  the clipping boundary.

.. c:function:: void termpaint_surface_write_with_len_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, int len, const termpaint_attr *attr, int clip_x0, int clip_x1)

  Like :c:func:`termpaint_surface_write_with_attr_clipped()` but does take a explicit length parameter instead of
  writing the string until it encounters a NUL character in the string.

.. c:function:: void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg)

  Like :c:func:`termpaint_surface_write_with_attr()` but with explicit parameters for foreground and background color.
  Decoration color will be set to TERMPAINT_DEFAULT_COLOR and no style attributes will be applied.

  See :ref:`colors` for how to specify colors.

.. c:function:: void termpaint_surface_write_with_len_colors(termpaint_surface *surface, int x, int y, const char *string, int len, int fg, int bg)

  Like :c:func:`termpaint_surface_write_with_colors()` but does take a explicit length parameter instead of
  writing the string until it encounters a NUL character in the string.

.. c:function:: void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1)

  Like :c:func:`termpaint_surface_write_with_attr_clipped()` but with explicit parameters for foreground and background color.
  Decoration color will be set to TERMPAINT_DEFAULT_COLOR and no style attributes will be applied.

  See :ref:`colors` for how to specify colors.

.. c:function:: void termpaint_surface_write_with_len_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1)

  Like :c:func:`termpaint_surface_write_with_colors_clipped()` but does take a explicit length parameter instead of
  writing the string until it encounters a NUL character in the string.

.. c:function:: void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg)

  Clear the contents of the whole surface. All cells are set to spaces with ``fg`` as foreground color and ``bg`` as
  background color. Decoration color will be set to TERMPAINT_DEFAULT_COLOR and no style attributes will be applied.

  The cleared cell will be marked as erased.

  See :ref:`colors` for how to specify colors.

.. c:function:: void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr)

  Clear the contents of the whole surface. All cells are set to spaces with attributes set to the contents of ``attr``.

  The cleared cell will be marked as erased.

.. c:function:: void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg)

  Like :c:func:`termpaint_surface_clear()` but only clears the rectangle starting from cell at ``x``, ``y`` in it's upper
  left corner with with width ``width`` and height ``height``.

  If clusters cross the boundary of the rectangle, these clusters are completely erased. Portions of these clusters
  outside of the rectangle preserve their colors and attributes.

  The cleared cell will be marked as erased.

  See :ref:`colors` for how to specify colors.

.. c:function:: void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr)

  Like :c:func:`termpaint_surface_clear_with_attr()` but only clears the rectangle starting from cell at ``x``, ``y``
  in it's upper left corner with with width ``width`` and height ``height``.

  If clusters cross the boundary of the rectangle, these clusters are completely erased. Portions of these clusters
  outside of the rectangle preserve their colors and attributes.

  The cleared cell will be marked as erased.

.. c:function:: void termpaint_surface_clear_with_char(termpaint_surface *surface, int fg, int bg, int codepoint)

  Like :c:func:`termpaint_surface_clear` but the cells will be filled with the character ``codepoint`` and marked as not erased.

.. c:function:: void termpaint_surface_clear_with_attr_char(termpaint_surface *surface, const termpaint_attr *attr, int codepoint)

  Like :c:func:`termpaint_surface_clear_with_attr` but the cells will be filled with the character ``codepoint`` and marked as not erased.

.. c:function:: void termpaint_surface_clear_rect_with_char(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg, int codepoint)

  Like :c:func:`termpaint_surface_clear_rect` but the cells will be filled with the character ``codepoint`` and marked as not erased.

.. c:function:: void termpaint_surface_clear_rect_with_attr_char(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr, int codepoint)

  Like :c:func:`termpaint_surface_clear_rect_with_attr` but the cells will be filled with the character ``codepoint`` and marked as not erased.

.. c:function:: void termpaint_surface_set_fg_color(const termpaint_surface *surface, int x, int y, unsigned fg)

  Replaces the foreground color of the cluster at position ``x``, ``y`` with the color given in ``fg``.

.. c:function:: void termpaint_surface_set_bg_color(const termpaint_surface *surface, int x, int y, unsigned bg)

  Replaces the background color of the cluster at position ``x``, ``y`` with the color given in ``bg``.

.. c:function:: void termpaint_surface_set_deco_color(const termpaint_surface *surface, int x, int y, unsigned deco_color)

  Replaces the decoration color of the cluster at position ``x``, ``y`` with the color given in ``deco_color``.

.. c:function:: void termpaint_surface_set_softwrap_marker(termpaint_surface *surface, int x, int y, _Bool state)

  This function sets or remove a soft wrap marker in a given cell. If ``state`` is true, the marker is set, otherwise
  it is removed. The marker will also be removed by writing new text to the cell or using a clear function.

  See above for how soft wrap markers are used to connect lines into paragraphs for native terminal
  clipboard functionality.

.. c:function:: void termpaint_surface_copy_rect(termpaint_surface *src_surface, int x, int y, int width, int height, termpaint_surface *dst_surface, int dst_x, int dst_y, int tile_left, int tile_right)

  Copies the contents of the rectangle with the upper-left corner ``x``, ``y`` and width ``width`` and height ``height``
  in surface ``surc_surface`` into the surface ``dst_surface`` at position ``dst_x``, ``dst_y``.

  if clusters in the source or destination surface cross the boundary of the rectangle the behavior depends on the
  values in ``tile_left`` for the left boundary and ``tile_right`` for the right boundary.

  The following tiling modes are available:

    .. c:namespace:: 0
    .. c:macro:: TERMPAINT_COPY_NO_TILE

      Partial clusters in the source are copied to the destination as spaces for the parts of the cluster that is inside
      the rectangle. If clusters in the destination cross the boundary they are erased before the copy is made. (The
      part of the cluster outside the rectangle preserves it's attributes but the text is replaced by spaces)

    .. c:macro:: TERMPAINT_COPY_TILE_PUT

      Clusters in the source will be copied into the destination even if that means modifying cells outside of the
      destination rectangle. This allows copying a larger region in multiple steps.

    .. c:macro:: TERMPAINT_COPY_TILE_PRESERVE

      If clusters in the destination line up with clusters in source, the cluster in the destination is preserved. This
      allows seamlessly extending a copy made with ``TERMPAINT_COPY_TILE_PUT`` without overwriting previously copied
      cells.

.. c:function:: void termpaint_surface_tint(termpaint_surface *surface, void (*recolor)(void *user_data, unsigned *fg, unsigned *bg, unsigned *deco), void *user_data)

  Changes the colors of all cells of the surface according to the recoloration function ``recolor``.

  This function is called for each cluster with the ``user_data`` and a pointer to locations that hold the foreground,
  background and decoration colors of that cluster. The function can then recolor that cluster by changing the values
  pointed to.

.. c:function:: unsigned termpaint_surface_peek_fg_color(const termpaint_surface *surface, int x, int y)

  Return the foreground color of the cluster at ``x``, ``y``.

.. c:function:: unsigned termpaint_surface_peek_bg_color(const termpaint_surface *surface, int x, int y)

  Return the background color of the cluster at ``x``, ``y``.

.. c:function:: unsigned termpaint_surface_peek_deco_color(const termpaint_surface *surface, int x, int y)

  Return the decoration color of the cluster at ``x``, ``y``.

.. c:function:: int termpaint_surface_peek_style(const termpaint_surface *surface, int x, int y)

  Return the style of the cluster at ``x``, ``y``.

.. c:function:: void termpaint_surface_peek_patch(const termpaint_surface *surface, int x, int y, const char **setup, const char **cleanup, _Bool *optimize)

  Place the low level patching information of the cluster at ``x``, ``y`` into to locations pointed to by ``setup``,
  ``cleanup`` and ``optimize``. The strings are owned by the surface and must not be freed.

.. c:function:: const char *termpaint_surface_peek_text(const termpaint_surface *surface, int x, int y, int *len, int *left, int *right)

  Return the text of the cluster at ``x``, ``y``. The returned string is not null terminated. It's length is stored into
  the location pointed to by ``len``. If non-zero the locations pointed to by ``left`` and ``right`` receive the
  columns of the left most and right most cell that is part of the cluster.

  If a cell is cleared this function returns a pointer to the special character :c:macro:`TERMPAINT_ERASED`.

.. c:function:: _Bool termpaint_surface_peek_softwrap_marker(const termpaint_surface *surface, int x, int y)

  This function returns true if a soft wrap marker is set in a given cell.

.. c:function:: _Bool termpaint_surface_same_contents(const termpaint_surface *surface1, const termpaint_surface *surface2)

  Compares two surfaces. If both have the same contents and attributes for every cell/cluster then it returns true.

.. c:function:: int termpaint_surface_char_width(const termpaint_surface *surface, int codepoint)

  Returns the "width" of a character with Unicode codepoint ``codepoint``.

  Prefer the :doc:`string measurement functions<measuring>` to using this function directly.

  Return values are

    0
      This character combines with previous characters into a cluster

    1
      This character takes one cell of space.

    2
      This character takes two cells of space.
