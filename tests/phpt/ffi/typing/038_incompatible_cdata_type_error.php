@kphp_should_fail
/assign FFI\\CData_Int64 to Foo::\$int32/
/declared as @var FFI\\CData_Int32/
<?php

class Foo {
  /** @var \FFI\CData_Int32 $x */
  public $int32;
}

$int64 = FFI::new('int64_t');
$foo = new Foo();
$foo->int32 = $int64;
