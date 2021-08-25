<?php

require_once __DIR__ . '/include/common.php';

kphp_load_vector_lib();

function test_printf1() {
  $cdef = FFI::cdef('
    int printf(const char *format, ...);
  ');

  $cdef->printf("hello\n");

  $cdef->printf("x=%ld\n", 10);
  $cdef->printf("x=%ld\n", PHP_INT_MAX);
  $cdef->printf("x=%ld y=%f\n", PHP_INT_MAX, 1.55);
  $cdef->printf(" %ld %f %ld %f \n", 1, 1.9, 1, 1.8);

  $cdef->printf("%s%s\n", "x", "y");

  $cdef->printf("%s: %0*d\n", implode('', ['ex', 'ample']), 5, 3);

  $int = FFI::new('int');
  $int->cdata = 5;
  $cdef->printf("%3ld\n", $int->cdata);

  $cdef->printf("true=%d false=%d int=%ld float=%.2f\n",
    true, false, 43, 43.0);
}

function test_printf2() {
  $cdef = FFI::cdef('
    struct Foo {
      int8_t i8;
      int32_t i32;
      int64_t i64;
      bool b;
    };
    struct Bar {
      struct Foo foo;
      struct Foo *foo_ptr;
    };
    int printf(const char *format, ...);
  ');

  $foo = $cdef->new('struct Foo');
  $bar = $cdef->new('struct Bar');
  $bar->foo = $foo;
  $bar->foo_ptr = FFI::addr($bar->foo);

  $foo->i8 = 10;
  $cdef->printf("%d %d %d\n", $foo->i8, $bar->foo->i8, $bar->foo_ptr->i8);
  $bar->foo->i8 = 20;
  $bar->foo->i32 = 4215;
  $bar->foo->i64 = 2941529834;
  $bar->foo->b = true;

  $cdef->printf("%d %d %ld %d\n",
    $bar->foo->i8, $bar->foo->i32, $bar->foo->i64, $bar->foo->b);
  $cdef->printf("%d %d %ld %d\n",
    $foo->i8, $foo->i32, $foo->i64, $foo->b);
}

function test_vector1() {
  $lib = FFI::scope('vector');
  $arr = $lib->new('struct Vector2Array');
  $lib->vector2_array_alloc(FFI::addr($arr), 3);
  print_vec_array($arr);
  $lib->vector2_array_fill(FFI::addr($arr),
    1.0, 2.0,
    5.0, 5.5,
    99.1, 99.2);
  print_vec_array($arr);
  $lib->vector2_array_fill(FFI::addr($arr),
      1.0, 2.0,
      0.0, 0.0,
      2.0, 1.0);
  print_vec_array($arr);
  $lib->vector2_array_free(FFI::addr($arr));
}

function test_vector2() {
  $lib = FFI::scope('vector');
  $arr = $lib->new('struct Vector2Array');

  $vec1 = $lib->new('struct Vector2');
  $vec1->x = 10.0;
  $vec1->y = 35.0;
  $vec2 = $lib->new('struct Vector2');
  $vec2->x = -4.5;
  $vec2->y = -1.9;

  $lib->vector2_array_init(FFI::addr($arr), 2, $vec1, $vec2);
  print_vec_array($arr);
  $lib->vector2_array_free(FFI::addr($arr));

  $lib->vector2_array_init_ptr(FFI::addr($arr), 2, FFI::addr($vec2), FFI::addr($vec1));
  print_vec_array($arr);
  $lib->vector2_array_free(FFI::addr($arr));
}

for ($i = 0; $i < 5; ++$i) {
  test_printf1();
  test_printf2();

  // PHP would require a flush() call to make C printf output stable.
#ifndef KPHP
    flush();
#endif

  test_vector1();
  test_vector2();
}
