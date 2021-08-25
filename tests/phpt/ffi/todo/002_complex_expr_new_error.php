@kphp_should_fail
<?php

function test() {
  $cdef = FFI::cdef('struct Foo { int i; };');
  var_dump($cdef->new('struct Foo')->i);
  $cdef->new('struct Foo')->i = 10;
}

test();
