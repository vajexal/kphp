@kphp_should_fail
/Invalid property access ...->cdata: does not exist in class/
<?php

$cdef = FFI::cdef('struct Foo { int x; };');
$foo = $cdef->new('struct Foo');
var_dump($foo->x->cdata);
