@kphp_should_fail
/Invalid property access ...->badname: does not exist in class/
<?php

$cdef = FFI::cdef('struct Foo { bool field; };');

$foo = $cdef->new('struct Foo');
$foo->badname;
