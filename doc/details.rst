Technical details
=================

.. _resync:

Handling of ambiguous input
---------------------------

Terminal to application communicating doesn't use a prefix free
encoding. For example the escape key sends just ASCII 0x1b, but many function
key sequences also begin with ASCII 0x1b. As the terminal communication is just
a stream of bytes the library needs to use some strategy to decide when no
further input is considered part of the input sequence.

Many terminal interface libraries use timeouts to disambiguate input. Which on
one hand makes behavior timing dependent and too large timeouts interfere with
using ESC followed by any other key.

Termpaint doesn't use timeouts in the traditional way. Instead when termpaint
encounters a sequence that needs disambiguation, it sends a request to the
terminal which produces a fixed reply that allows to disambiguate the sequence.

In practical terms the sequence used is ``ESC[5n`` to which the terminal replies
``ESC[0n``.

The concrete implementation uses a short delay before sending ``ESC[5n`` to
reduce overhead. It also only ever keeps one such request in flight at one time.


.. _safety:

Safety
------

There are various rules an application has to follow to safely interact with
functions in termpaint.

Parameters
----------

* unless otherwise specified all pointers need to point to valid objects of
  their type
* pointers to opaque structures always need to point to a initialized and
  valid object. (Exceptions are of course functions that initialize the
  objects)

Life times
..........

Objects derived from the terminal object need to have a bounded lifetime that
does not exceed the lifetime of the terminal object.

The application is responsible for freeing all subobjects before freeing the
terminal object.

Integrations have to take care not to call into the terminal object after it
was freed.

Threading and signals
.....................

No function in termpaint is async signal safe. Applications must cache all
needed information for signal handlers themselves.

All objects that are created (transitively) from a terminal and the
terminals integration need to be handled as one item for threading. This
whole collection of objects may only be called from a one thread at a time.

Switching the thread that calls into this collection needs to be mediated by
a C happens-before relation.

Independent terminal instances can be used without interfering with each other.

.. _incremental-update:

Terminal update optimization
----------------------------

Each call to :c:func:`termpaint_terminal_flush` takes a copy of the just
flushed primary surface. If the next call to ``flush`` does not specify
a full redraw, only cells are redrawn that differ from the copy made in
the previous call.

.. _malloc-failure:

Environments that need to handle malloc failure
-----------------------------------------------

A restricted subset of termpaint can be used in environments that need to
handle malloc failure gracefully.

The default mode of operation of termpaint is to abort() on allocation
failure for ease of usage and to avoid cluttering code using termpaint
with checks. For many applications that are not carefully written to
handle allocation failure this is a reasonable tradeoff. For applications
that are written carefully enough to handle allocation failures there is
an alternative mode, that avoids aborting.

For these applications three major differences in usage are needed.

The first difference is that for all functions where a variant ending with
``_or_nullptr`` or ``_mustcheck`` is offered the application needs to call
these variants. These signal via a return value of NULL or false that
memory allocation failed. The application can try to work without their
effects or try calling them again later. Consult the header files for
which functions need these variants.

The next difference is that applications need to call
:c:func:`termpaint_terminal_glitch_on_out_of_memory` to switch termpaint
to a mode where it does not abort, but potentially misrenders output when
memory allocation fails in code paths that can't sensibly report an error.
The major parts where this can happen is when writing strings that need
more than 8 byte of utf8 encoded characters per cell (i.e. stacks of
composing characters or complex emoji sequences) or when using
:c:func:`termpaint_attr_set_patch`.

The last difference is that the application may only use a limited subset
of termpaint.

Currently there are the following known restrictions:

* :c:func:`termpaint_surface_copy_rect` must not be used with the same
  surface for source and destination. As workaround preallocate a
  surface to use as an intermediate surface for such copy operations.

* termpaintx is not yet allocation failure safe.

Some parts of termpaint like terminal type auto-detection and management
of the restore sequence use preallocated memory to avoid memory allocation
failures in many code paths. The preallocations might not be large enough
if a terminal sends excessive amounts of identifying data or the application
uses an extremely high amount of setup (i.e. more colors slots than predefined).

