@kphp_should_fail
/Unknown function ->g\(\) of scope\$example/
<?php

$cdef = FFI::cdef('
  #define FFI_SCOPE "example"
  void f();
');

$cdef->g();
