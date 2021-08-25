@kphp_should_fail
/Invalid php2c conversion: FFI\\CData_Int8\* -> int16_t\*/
<?php

$cdef = FFI::cdef('
  void f(int16_t *x);
');

$x = FFI::new('int8_t');
$cdef->f(FFI::addr($x));
