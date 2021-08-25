// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "runtime/ffi.h"

#include <mutex>
#include <dlfcn.h>

FFIEnv ffi_env_instance;

FFIEnv::FFIEnv(int num_libs, int num_symbols): num_libs{num_libs}, num_symbols{num_symbols} {
  libs = new SharedLib[num_libs];
  symbols = new Symbol[num_symbols];
}

bool FFIEnv::is_shared_lib_loaded(int id) {
  return libs[id].handle != nullptr;
}

void FFIEnv::load_shared_lib(int id) {
  void *handle = funcs.dlopen(libs[id].path, RTLD_LAZY);
  if (!handle) {
    php_critical_error("can't open %s library", libs[id].path);
  }
  libs[id].handle = handle;
}

void FFIEnv::load_symbol(int lib_id, int dst_sym_id) {
  const char *symbol_name = symbols[dst_sym_id].name;
  void *symbol = funcs.dlsym(libs[lib_id].handle, symbol_name);
  if (!symbol) {
    php_warning("%s library doesn't export %s symbol", libs[lib_id].path, symbol_name);
    return;
  }
  symbols[dst_sym_id].ptr = symbol;
}

class_instance<C$FFI$Scope> f$FFI$$load(const string &filename __attribute__ ((unused))) {
  return class_instance<C$FFI$Scope>().empty_alloc();
}

class_instance<C$FFI$Scope> f$FFI$$scope(const string &scope_name __attribute__ ((unused))) {
  return class_instance<C$FFI$Scope>().empty_alloc();
}

class_instance<C$FFI$Scope> ffi_load_scope_symbols(class_instance<C$FFI$Scope> instance, int shared_lib_id, int sym_offset, int num_symbols) {
  // TODO: we can have N mutexes, per every shared_lib_id, so we don't block all load_scope calls;
  // but in practice, it probably doesn't matter as load_scope() will return early during the second call
  static std::mutex mutex;
  std::lock_guard<std::mutex> guard{mutex};

  if (ffi_env_instance.is_shared_lib_loaded(shared_lib_id)) {
    // a sanity check: library symbols should be initialized at this point
    php_assert(ffi_env_instance.symbols[sym_offset].ptr != nullptr);
    return instance;
  }

  ffi_env_instance.load_shared_lib(shared_lib_id);

  // a sanity check: library symbols should not be initialized
  php_assert(ffi_env_instance.symbols[sym_offset].ptr == nullptr);

  // bind all symbols; this should happen exactly once per every shared lib
  for (int i = sym_offset; i < sym_offset + num_symbols; i++) {
    ffi_env_instance.load_symbol(shared_lib_id, i);
  }

  return instance;
}
