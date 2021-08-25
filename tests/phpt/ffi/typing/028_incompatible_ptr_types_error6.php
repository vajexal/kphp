@kphp_should_fail
/pass FFI\\CData_Int8\*\* to argument/
/declared as @param FFI\\CData_Int8\*/
<?php

$cdef = FFI::cdef('
  struct Foo {
    int8_t **x;
  };
  int f(int8_t *ptr);
');

$foo = $cdef->new('struct Foo');
$cdef->f($foo->x);
