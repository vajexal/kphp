// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include <vector>
#include <string>

#include "compiler/ffi/ffi_types.h"
#include "compiler/data/class-data.h"

// FFI symbol is an extern variable or function information carrier
struct FFISymbol {
  // env_index is bound after FFIRoot.bind_symbols() is completed;
  // it's an index inside FFIEnv.symbols array (runtime structure)
  int env_index = -1;

  const FFIType *type = nullptr;

  FFISymbol(const FFIType *type): type{type} {}

  std::string name() const { return type->str; }

  bool operator <(const FFISymbol& other) const { return name() < other.name(); }
};

struct FFIClassDataMixin {
  FFIClassDataMixin(const FFIType *ffi_type, std::string scope_name, ClassPtr non_ref = {})
    : non_ref{non_ref}
    , ffi_type{ffi_type}
    , scope_name{std::move(scope_name)} {}

  bool is_ref() const noexcept { return static_cast<bool>(non_ref); }

  ClassPtr non_ref; // for reference types this field will contain the original (non-referential) type
  const FFIType *ffi_type;
  std::string scope_name;
};

struct FFIScopeDataMixin {
  std::string scope_name;

  Location location;

  std::string static_lib;

  // -1 means that all symbols originate from a static library;
  // note that we can store the shared lib id only once as opposed to
  // a per-symbol alternative because we limit scopes to 1 source definition
  int shared_lib_id = -1;

  std::vector<FFISymbol> variables;
  std::vector<FFISymbol> functions;
  std::vector<const FFIType*> types; // not sorted!

  std::map<std::string, int> enum_constants;

  FFITypedefs typedefs;

  bool is_shared_lib() const noexcept { return shared_lib_id != -1; }

  int get_env_offset() const noexcept {
    // variables are placed before functions, so if there
    // are any variables, we use the first variable offset
    if (!variables.empty()) {
      return variables[0].env_index;
    }
    kphp_assert(!functions.empty());
    return functions[0].env_index;
  }

  const FFISymbol *find_variable(const std::string &name) { return find_symbol(variables, name); }
  const FFISymbol *find_function(const std::string &name) { return find_symbol(functions, name); }

private:

  static const FFISymbol *find_symbol(const std::vector<FFISymbol> &sorted_vector, const std::string &name) {
    const auto &it = std::lower_bound(sorted_vector.begin(), sorted_vector.end(), name, [](const FFISymbol &lhs, const std::string &name) {
      return lhs.name() < name;
    });
    if (it != sorted_vector.end() && it->name() == name) {
      return &sorted_vector[std::distance(sorted_vector.begin(), it)];
    }
    return nullptr;
  }
};

struct FFISharedLib {
  int id;
  std::string path;
};

class FFIRoot {
  std::mutex mutex;

  std::vector<FFISharedLib> shared_libs;
  std::unordered_map<std::string, FFIScopeDataMixin*> scopes;

  int num_dynamic_symbols = 0;

public:

  const TypeHint *c2php_field_type_hint(const TypeHint *c_hint);
  const TypeHint *c2php_return_type_hint(const TypeHint *c_hint);
  const TypeHint *c2php_scalar_type_hint(ClassPtr cdata_class);

  const TypeHint *create_type_hint(const FFIType *type, const std::string &scope_name);

  const std::vector<FFISharedLib> &get_shared_libs() const;
  int get_shared_lib_id(const std::string &path);

  std::vector<FFIScopeDataMixin*> get_scopes() const;
  FFIScopeDataMixin* find_scope(const std::string &scope_name);

  const FFIType *get_ffi_type(ClassPtr klass);
  const FFIType *get_ffi_type(const TypeHint *type_hint);

  bool register_scope(const std::string &scope_name, FFIScopeDataMixin *data);

  void bind_symbols();

  int get_dynamic_symbols_num() const noexcept { return num_dynamic_symbols; }

  static std::string scope_class_name(const std::string &scope_name) {
    return "scope$" + scope_name;
  }

  static std::string cdata_class_name(const std::string &scope_name, const std::string &name) {
    return "cdata$" + scope_name + "\\" + name;
  }

private:

  enum class Php2cMode {
    FuncReturn,
    FieldRead,
  };
  const TypeHint *c2php_type_hint(const TypeHint *c_type, Php2cMode mode);
  const TypeHint *c2php_scalar_type_hint(FFITypeKind kind);
};
