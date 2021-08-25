@kphp_should_fail
<?php

function test() {
  $cdef = FFI::cdef('struct Foo { int i; };');
  var_dump(FFI::addr($cdef->new('struct Foo'))->i);
  FFI::addr($cdef->new('struct Foo'))->i = 20;
}

test();
