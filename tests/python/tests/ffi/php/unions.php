<?php

function test1() {
  $cdef = FFI::cdef('
    typedef union {
      int32_t i32;
      int16_t i16;
    } Example;
  ');

  $u = $cdef->new('Example');
  $u->i32 = 14300130;
  var_dump($u->i16);
  var_dump($u->i32);
  $u->i16 = 999;
  var_dump($u->i16);
  var_dump($u->i32);
}

function test2() {
  $cdef = FFI::cdef('
    struct Foo {
      int8_t x;
      int32_t y;
      int64_t z;
    };

    union MyUnion {
      float f32;
      struct Foo foo;
      double f64;
    };
  ');

  $u = $cdef->new('union MyUnion');
  $u->f32 = 1.5;
  printf("%.5f\n", $u->f32);
  printf("%.5f\n", $u->f64);
  var_dump($u->foo->x);
  var_dump($u->foo->y);
  var_dump($u->foo->z);
  $u->foo->y = 9542394821843;
  printf("%.5f\n", $u->f32);
  printf("%.5f\n", $u->f64);
  var_dump($u->foo->x);
  var_dump($u->foo->y);
  var_dump($u->foo->z);
}

function test3() {
  $cdef = FFI::cdef('
    union Value {
      int32_t int_val;
      bool bool_val;
    };
    struct Foo {
      union Value val;
    };
  ');

  $foo = $cdef->new('struct Foo');
  $foo->val->int_val = 1;
  var_dump($foo->val->int_val);
  var_dump($foo->val->bool_val);
  $foo->val->bool_val = false;
  var_dump($foo->val->int_val);
  var_dump($foo->val->bool_val);
  $foo->val->int_val = 124;
  var_dump($foo->val->int_val);
  var_dump($foo->val->bool_val);
}

test1();
test2();
test3();
