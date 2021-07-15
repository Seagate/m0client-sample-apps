Preparing computations library
==============================

Computations from an external library cannot be linked directly with
a Motr instance. The library is supposed to have an entry function named
``void motr_lib_init(void)``. All the computations in the library must
have the following signature::

  int comp(struct m0_buf *args, struct m0_buf *out,
           struct m0_isc_comp_private *comp_data, int *rc)

See isc_libdemo.c for examples.

Loading the library
===================

With ``spiel`` command (see spiel/spiel.h and c0appz_isc.h) the library
can be loaded with any running Motr instance. A helper function
``c0appz_isc_api_register`` (see c0appz_isc.h) takes the library path which
is (IMPORTANT!) expected to be the same across all the nodes running Motr.

Currently, a utility ``c0isc_reg`` takes the path as an input and loads
the library into all the remote Motr instances.

On successful loading of the library, the output will look like this::

  $ ./c0isc_reg $PWD/libdemo.so
  c0isc_reg success

Demo computations
=================

Currently, we demonstrate three simple computations: ``ping``, ``min`` and
``max``. ``c0isc_demo`` utility can be used to invoke the computations and
see the result::

  $ ./c0isc_demo

  Usage: c0isc_demo [-v[v]] COMP OBJ LEN

    Supported COMPutations: ping, min, max.

    OBJ is two uint64 numbers in format: hi:lo.
    LEN is the length of object (in KiB).

To build the utility and ``libdemo.so`` library, run::

  make isc-all

Following are the steps to view the demo output.

ping
----

This functionality pings all the ISC services spanned by the object units.
For each unit a separate ping request is sent, so the utility prints
"Hello-World@<service-fid>" reply each of these requests.

Here is an example for the object with 1MB units::

  $ ./c0isc_demo ping 123:12371 4096
  Hello-world @192.168.180.171@tcp:12345:2:2
  Hello-world @192.168.180.171@tcp:12345:2:2
  Hello-world @192.168.180.171@tcp:12345:2:2
  Hello-world @192.168.180.171@tcp:12345:2:2

Note: the object length (or the amount to read) must be specified, as Motr
does not store the objects lengths in their metadata.

min / max
---------

Write an object with a real numbers strings delimited by the newline.
The min/max in-storage computation can then be done on such object::

  $ ./c0isc_demo max 123:12371 4096
  idx=132151 val=32767.627900
  $ ./c0isc_demo min 123:12371 4096
  idx=180959 val=0.134330
