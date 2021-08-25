<?php

function test() {
  $cdef = FFI::cdef('
    struct Foo {
      const char *s;
    };
    struct Bar {
      char c;
    };
  ');
  $foo = $cdef->new('struct Foo');
  $foo->s = "hello";
  var_dump($foo->x);
}

test();
