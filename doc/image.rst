Addon image
===========

The image addon contains functions to store the contents of a :c:type:`termpaint_surface` into a file or memory buffer
and to load the contents from a file or memory buffer.

This addon is only available if your compilation environment supports c++.

Functions
---------

See :ref:`safety` for general rules for calling functions in termpaint.

.. c:function:: _Bool termpaint_image_save(termpaint_surface *surface, const char* name)

  Save the contents if the surface ``surface`` into a file with the name ``name``.

.. c:function:: bool termpaint_image_save_to_file(termpaint_surface *surface, FILE *file)

  Save the contents if the surface ``surface`` into a file referred to by file pointer ``file``.

.. c:function:: char *termpaint_image_save_alloc_buffer(termpaint_surface *surface)

  Save the contents if the surface ``surface`` into a freshly allocated nul-terminated buffer.

  After usage the returned buffer must be deallocated using termpaint_image_save_dealloc_buffer.

.. c:function:: void termpaint_image_save_dealloc_buffer(char *buffer)

  Deallocate a buffer returned by termpaint_image_save_alloc_buffer.

.. c:function:: termpaint_surface *termpaint_image_load(termpaint_terminal *term, const char *name)

  Load the contents of a surface from the file named ``name`` and return a newly allocated surface with the data
  from the file prepared to be used with the terminal object ``term``.

.. c:function:: termpaint_surface *termpaint_image_load_from_file(termpaint_terminal *term, FILE *file)

  Load the contents of a surface from the file referred to by file pointer ``file`` and return a newly allocated surface
  with the data from the file prepared to be used with the terminal object ``term``.

.. c:function:: termpaint_surface *termpaint_image_load_from_buffer(termpaint_terminal *term, char *buffer, int length)

  Load the contents of a surface from a memory buffer ``buffer`` with the length ``length`` and return a newly
  allocated surface with the data from the file prepared to be used with the terminal object ``term``.
