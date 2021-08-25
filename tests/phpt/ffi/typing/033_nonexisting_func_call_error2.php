@kphp_should_fail
/Unknown function ->g\(\) of scope\$cdef\$u[a-f0-9]+_0/
<?php

$cdef = FFI::cdef('
  void f();
');

$cdef->g();
