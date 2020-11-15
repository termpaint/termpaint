Addon image
===========

The image addon contains functions to store the contents of a :c:type:`termpaint_surface` into a file and to load the
contents from a file.

This addon is only available if your compilation environment supports c++.

Functions
---------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: _Bool termpaint_image_save(termpaint_surface *surface, const char* name)

  Save the contents if the surface ``surface`` into a file with the name ``name``.

.. c:function:: termpaint_surface *termpaint_image_load(termpaint_terminal *term, const char *name)

  Load the contents of a surface from the file named ``name`` and return a newly allocated surface with the data
  from the file prepared to be used with the terminal object ``term``.
