@kphp_should_fail
/pass FFI\\CData_Char\* to argument/
/declared as @param string/
<?php

function test() {
  $cdef = FFI::cdef('
    struct Foo {
      const char *s;
    };
  ');
  $foo = $cdef->new('struct Foo');
  f($foo->s);
}

function f(string $s) {}

test();
