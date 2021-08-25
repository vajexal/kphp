// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "compiler/ffi/ffi_types.h"

#include <unordered_map>

static std::string format_list(std::vector<FFIType*>::const_iterator begin, std::vector<FFIType*>::const_iterator end, char sep = ',') {
  std::string result;
  for (auto it = begin; it != end; ++it) {
    result += (*it)->to_string();
    if (std::next(it) != end) {
      result.push_back(sep);
      result.push_back(' ');
    }
  }
  return result;
}

static std::string format_struct_or_union_def(const FFIType *s, const std::string &tag) {
  return tag + " " + s->str + "{ " + format_list(s->members.begin(), s->members.end(), ';') + "; }";
}

static std::string format_function(const FFIType *f) {
  std::string variadic_arg = f->is_variadic() ? ", ..." : "";
  return f->members[0]->to_string() + " " + f->str + "(" + format_list(f->members.begin() + 1, f->members.end()) + variadic_arg + ")";
}

static std::string var_to_string(const FFIType *var) {
  const FFIType *type = var->members[0];
  std::string name = var->str;
  std::string array_suffix;

  while (type->kind == FFITypeKind::Array) {
    array_suffix = "[" + std::to_string(type->num) + "]" + array_suffix;
    type = type->members[0];
  }

  return type->to_string() + " " + name + array_suffix;
}

static FFIBuiltinType make_builtin_type(FFITypeKind kind, std::string php_class_name, std::string c_name) {
  std::string src_name = "C$FFI$CData<" + c_name + ">";
  return FFIBuiltinType{
    FFIType{kind},
    std::move(php_class_name),
    std::move(c_name),
    std::move(src_name),
  };
}

static std::vector<FFIBuiltinType> builtin_types = {
  make_builtin_type(FFITypeKind::Void, "FFI\\CData_Void", "void"),
  make_builtin_type(FFITypeKind::Bool, "FFI\\CData_Bool", "bool"),
  make_builtin_type(FFITypeKind::Char, "FFI\\CData_Char", "char"),
  make_builtin_type(FFITypeKind::Float, "FFI\\CData_Float", "float"),
  make_builtin_type(FFITypeKind::Double, "FFI\\CData_Double", "double"),
  make_builtin_type(FFITypeKind::Int8, "FFI\\CData_Int8", "int8_t"),
  make_builtin_type(FFITypeKind::Int16, "FFI\\CData_Int16", "int16_t"),
  make_builtin_type(FFITypeKind::Int32, "FFI\\CData_Int32", "int32_t"),
  make_builtin_type(FFITypeKind::Int64, "FFI\\CData_Int64", "int64_t"),
  make_builtin_type(FFITypeKind::Uint8, "FFI\\CData_Uint8", "uint8_t"),
  make_builtin_type(FFITypeKind::Uint16, "FFI\\CData_Uint16", "uint16_t"),
  make_builtin_type(FFITypeKind::Uint32, "FFI\\CData_Uint32", "uint32_t"),
  make_builtin_type(FFITypeKind::Uint64, "FFI\\CData_Uint64", "uint64_t"),

  make_builtin_type(FFITypeKind::Unknown, "FFI\\CData", "cdata"),
  make_builtin_type(FFITypeKind::Unknown, "", "unknown"),
};

std::string format_type(const FFIType *type) {
  std::string result;

  if (const auto *builtin = ffi_builtin_type(type->kind)) {
    result = builtin->c_name;
  } else {
    switch (type->kind) {
      case FFITypeKind::StructDef:
        result = format_struct_or_union_def(type, "struct");
        break;
      case FFITypeKind::UnionDef:
        result = format_struct_or_union_def(type, "union");
        break;
      case FFITypeKind::Field:
        return var_to_string(type);
      case FFITypeKind::Union:
        result = "union " + type->str;
        break;
      case FFITypeKind::Struct:
        result = "struct " + type->str;
        break;
      case FFITypeKind::Enum:
        result = "enum " + type->str;
        break;
      case FFITypeKind::Array:
        result = type->members[0]->to_string() + "[" + std::to_string(type->num) + "]";
        break;
      case FFITypeKind::Pointer:
        result = type->members.empty() ? "*" : type->members[0]->to_string() + std::string(type->num, '*');
        break;
      case FFITypeKind::Function:
        result = format_function(type);
        break;
      default:
        result = "?";
        break;
    }
  }

  return result;
}

std::string FFIType::to_string() const {
  std::string result = format_type(this);

  if (flags & FFIType::Flag::Volatile) {
    result = "volatile " + result;
  }
  if (flags & FFIType::Flag::Const) {
    result = "const " + result;
  }

  return result;
}

std::string FFIType::to_decltype_string() const {
  if (kind == FFITypeKind::Function) {
    std::string variadic_arg = is_variadic() ? ", ..." : "";
    return members[0]->to_string() + " (*)(" + format_list(members.begin() + 1, members.end()) + variadic_arg + ")";
  }
  if (kind == FFITypeKind::StructDef) {
    return "struct " + str;
  }
  if (kind == FFITypeKind::UnionDef) {
    return "union " + str;
  }
  if (kind == FFITypeKind::Field) {
    return members[0]->to_string();
  }
  return format_type(this);
}

void ffi_type_delete(const FFIType *type) {
  for (const auto *member : type->members) {
    ffi_type_delete(member);
  }
  delete type;
}

const FFIBuiltinType *ffi_builtin_type(FFITypeKind kind) {
  for (auto &builtin_type : builtin_types) {
    if (builtin_type.type.kind == kind) {
      return &builtin_type;
    }
  }
  return nullptr;
}

const FFIBuiltinType *ffi_builtin_type(const std::string &php_class_name) {
  for (auto &builtin_type : builtin_types) {
    if (builtin_type.php_class_name == php_class_name) {
      return &builtin_type;
    }
  }
  return nullptr;
}
