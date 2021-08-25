// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "compiler/data/ffi-data.h"

#include "compiler/type-hint.h"
#include "common/algorithms/contains.h"

const FFIType *FFIRoot::get_ffi_type(const TypeHint *type_hint) {
  if (const auto *as_instance = type_hint->try_as<TypeHintInstance>()) {
    return get_ffi_type(as_instance->to_type_data()->class_type());
  }
  if (const auto *as_ffi = type_hint->try_as<TypeHintFFIType>()) {
    return as_ffi->type;
  }
  return nullptr;
}

const FFIType *FFIRoot::get_ffi_type(ClassPtr klass) {
  if (klass->ffi_class_mixin) {
    return klass->ffi_class_mixin->ffi_type;
  }
  return nullptr;
}

const TypeHint *FFIRoot::c2php_scalar_type_hint(ClassPtr cdata_class) {
  if (const auto *ffi_type = get_ffi_type(cdata_class)) {
    return c2php_scalar_type_hint(ffi_type->kind);
  }
  return nullptr;
}

const TypeHint *FFIRoot::c2php_return_type_hint(const TypeHint *c_hint) {
  return c2php_type_hint(c_hint, Php2cMode::FuncReturn);
}

const TypeHint *FFIRoot::c2php_field_type_hint(const TypeHint *c_hint) {
  return c2php_type_hint(c_hint, Php2cMode::FieldRead);
}

const TypeHint *FFIRoot::c2php_scalar_type_hint(FFITypeKind kind) {
  switch (kind) {
    case FFITypeKind::Int8:
    case FFITypeKind::Int16:
    case FFITypeKind::Int32:
    case FFITypeKind::Int64:
    case FFITypeKind::Uint8:
    case FFITypeKind::Uint16:
    case FFITypeKind::Uint32:
    case FFITypeKind::Uint64:
      return TypeHintPrimitive::create(tp_int);

    case FFITypeKind::Void:
      return TypeHintPrimitive::create(tp_void);

    case FFITypeKind::Bool:
      return TypeHintPrimitive::create(tp_bool);

    case FFITypeKind::Float:
    case FFITypeKind::Double:
      return TypeHintPrimitive::create(tp_float);

    default:
      return nullptr;
  }
}

const TypeHint *FFIRoot::c2php_type_hint(const TypeHint *c_hint, Php2cMode mode) {
  const FFIType *type = get_ffi_type(c_hint);
  if (!type) {
    return nullptr;
  }

  if (mode == Php2cMode::FuncReturn) {
    if (type->is_cstring()) {
      return TypeHintPrimitive::create(tp_string);
    }
  }

  if (const auto *as_scalar = c2php_scalar_type_hint(type->kind)) {
    return as_scalar;
  }

  switch (type->kind) {
    case FFITypeKind::Char:
      return TypeHintPrimitive::create(tp_string);

    case FFITypeKind::Pointer:
    case FFITypeKind::Array:
      return c_hint;

    case FFITypeKind::Struct:
    case FFITypeKind::StructDef:
      if (mode == Php2cMode::FieldRead) {
        if (const auto *as_instance = c_hint->try_as<TypeHintInstance>()) {
          return TypeHintInstance::create("&" + as_instance->full_class_name);
        }
        if (const auto *as_ffi = c_hint->try_as<TypeHintFFIType>()) {
          return TypeHintInstance::create("&" + cdata_class_name(as_ffi->scope_name, as_ffi->type->str));
        }
      }
      return c_hint;

    default:
      return nullptr;
  }
}

const TypeHint *FFIRoot::create_type_hint(const FFIType *type, const std::string &scope_name) {
  const auto *builtin_type = ffi_builtin_type(type->kind);
  if (builtin_type) {
    return TypeHintInstance::create(builtin_type->php_class_name);
  }
  switch (type->kind) {
    case FFITypeKind::Struct:
    case FFITypeKind::StructDef:
    case FFITypeKind::Union:
    case FFITypeKind::UnionDef:
      return TypeHintInstance::create(cdata_class_name(scope_name, type->str));

    case FFITypeKind::Pointer:
    case FFITypeKind::Array:
      return TypeHintFFIType::create(scope_name, type);

    default:
      return nullptr;
  }
}

std::vector<FFIScopeDataMixin*> FFIRoot::get_scopes() const {
  std::vector<FFIScopeDataMixin*> result;
  result.reserve(scopes.size());
  for (const auto &it : scopes) {
    result.emplace_back(it.second);
  }
  std::sort(result.begin(), result.end(), [](const auto &x, const auto &y) { return x->scope_name < y->scope_name; });
  return result;
}

FFIScopeDataMixin *FFIRoot::find_scope(const std::string &scope_name) {
  return scopes.at(scope_name);
}

bool FFIRoot::register_scope(const std::string &scope_name, FFIScopeDataMixin *data) {
  std::lock_guard<std::mutex> locker{mutex};

  if (vk::contains(scopes, scope_name)) {
    return false;
  }
  scopes.emplace(scope_name, data);
  return true;
}

int FFIRoot::get_shared_lib_id(const std::string &path) {
  std::lock_guard<std::mutex> locker{mutex};

  const auto &it = std::find_if(shared_libs.begin(), shared_libs.end(), [&](const FFISharedLib &x){
    return x.path == path;
  });
  if (it != shared_libs.end()) {
    return std::distance(shared_libs.begin(), it);
  }
  int id = shared_libs.size();
  shared_libs.emplace_back(FFISharedLib{id, path});
  return id;
}

const std::vector<FFISharedLib> &FFIRoot::get_shared_libs() const {
  return shared_libs;
}

void FFIRoot::bind_symbols() {
  int index = 0;
  for (auto &it : scopes) {
    auto &scope = it.second;
    for (auto &sym : scope->variables) {
      sym.env_index = index;
      index++;
    }
    for (auto &sym : scope->functions) {
      sym.env_index = index;
      index++;
    }
  }
  num_dynamic_symbols = index;
}
