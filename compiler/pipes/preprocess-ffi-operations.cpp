// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "compiler/pipes/preprocess-ffi-operations.h"

#include "compiler/const-manipulations.h"
#include "compiler/ffi/ffi_parser.h"
#include "compiler/type-hint.h"

static ClassPtr get_cdata_class(FunctionPtr context, VertexPtr expr) {
  ClassPtr klass = infer_class_of_expr(context, expr).try_as_class();
  if (klass && klass->is_ffi_cdata()) {
    return klass;
  }
  return {};
}

static bool is_ptr_field(ClassPtr klass, vk::string_view field_name) {
  const auto *field = klass->get_instance_field(field_name);
  if (!field) {
    return false;
  }
  const auto *as_ffi = field->type_hint->try_as<TypeHintFFIType>();
  if (!as_ffi) {
    return false;
  }
  return as_ffi->type->kind == FFITypeKind::Pointer;
}

VertexPtr PreprocessFFIOperationsBegin::on_ffi_cast(VertexAdaptor<op_func_call> call) {
  if (call->args().size() != 2) {
    return call;
  }
  const std::string &type_expr = collect_string_concatenation(call->args()[0]);
  kphp_error_act(!type_expr.empty(), "FFI::cast() $type param expects a non-empty const string argument", return call);

  FFITypedefs typedefs;
  auto [type, err] = ffi_parse_type(type_expr, typedefs);
  kphp_error_act(err.message.empty(),
                 fmt_format("FFI::cast(): line {}: {}", err.line, err.message),
                 return call);

  auto cast_op = VertexAdaptor<op_ffi_cast>::create(call->args()[1]).set_location(call);
  cast_op->php_type = G->get_ffi_root().create_type_hint(type, "C");
  return cast_op;
}

VertexPtr PreprocessFFIOperationsBegin::on_ffi_addr(VertexAdaptor<op_func_call> call) {
  if (call->args().size() != 1) {
    return call;
  }
  return VertexAdaptor<op_ffi_addr>::create(call->args()[0]).set_location(call);
}

static bool contains_non_scalar_types(const FFIType *type) {
  if (vk::any_of_equal(type->kind, FFITypeKind::Struct, FFITypeKind::Union)) {
    return true;
  }
  for (const FFIType *member : type->members) {
    if (contains_non_scalar_types(member)) {
      return true;
    }
  }
  return false;
}

VertexPtr PreprocessFFIOperationsBegin::on_ffi_static_new(VertexAdaptor<op_func_call> call) {
  if (call->args().size() != 1) {
    return call;
  }
  const std::string &type_expr = collect_string_concatenation(call->args()[0]);
  kphp_error_act(!type_expr.empty(), "FFI::new() $type param expects a non-empty const string argument", return call);

  FFITypedefs typedefs;
  auto [type, err] = ffi_parse_type(type_expr, typedefs);
  kphp_error_act(err.message.empty(),
                 fmt_format("FFI::new(): line {}: {}", err.line, err.message),
                 return call);

  kphp_error_act(!contains_non_scalar_types(type),
                 "static FFI::new() can only create scalar types", return call);

  auto new_op = VertexAdaptor<op_ffi_new>::create().set_location(call);
  new_op->php_type = G->get_ffi_root().create_type_hint(type, "C");
  return new_op;
}

VertexPtr PreprocessFFIOperationsBegin::on_ffi_new(VertexAdaptor<op_func_call> call, ClassPtr scope_class) {
  if (call->args().size() != 2) {
    return call;
  }
  const std::string &type_expr = collect_string_concatenation(call->args()[1]);
  kphp_error_act(!type_expr.empty(), "ffi->new() $type param expects a non-empty const string argument", return call);

  auto *scope = scope_class->ffi_scope_mixin;

  auto [type, err] = ffi_parse_type(type_expr, scope->typedefs);
  kphp_error_act(err.message.empty(),
                 fmt_format("ffi->new(): line {}: {}", err.line, err.message),
                 return call);

  auto new_op = VertexAdaptor<op_ffi_new>::create().set_location(call);
  new_op->php_type = G->get_ffi_root().create_type_hint(type, scope->scope_name);
  kphp_error_act(new_op->php_type, fmt_format("ffi->new(): failed to infer a PHP type hint for {} type", type->str), return call);

  return new_op;
}

VertexPtr PreprocessFFIOperationsBegin::on_exit_vertex(VertexPtr root) {
  if (auto call = root.try_as<op_func_call>()) {
    if (call->extra_type == op_ex_func_call_arrow) {
      ClassPtr klass = infer_class_of_expr(current_function, call->args()[0]).try_as_class();
      if (klass && klass->is_ffi_scope()) {
        if (call->get_string() == "new") {
          return on_ffi_new(call, klass);
        }
      }
    } else {
      const std::string func_name = get_full_static_member_name(current_function, call->get_string());
      if (func_name == "FFI$$new") {
        return on_ffi_static_new(call);
      }
      if (func_name == "FFI$$addr") {
        return on_ffi_addr(call);
      }
      if (func_name == "FFI$$cast") {
        return on_ffi_cast(call);
      }
    }
  }

  return root;
}

VertexPtr PreprocessFFIOperationsEnd::on_cdata_instance_prop(ClassPtr root_class, VertexAdaptor<op_instance_prop> root) {
  // instance() part is cdata, so we would need to wrap this vertex in
  // c2php conversion; we'll traverse the instance() to mark all
  // nested instance_op with appropriate access_type so we don't
  // try to insert conversions there as well

  VertexAdaptor<op_instance_prop> current = root;
  while (true) {
    auto next = current->instance().try_as<op_instance_prop>();
    if (!next) {
      // end of the chain
      current->access_type = InstancePropAccessType::CData;
      break;
    }
    ClassPtr cdata_class = get_cdata_class(current_function, next->instance());
    if (!cdata_class) {
      // left side instance is not FFI value, can't access directly
      current->access_type = InstancePropAccessType::CData;
      break;
    }
    current->access_type = is_ptr_field(cdata_class, next->get_string())
                           ? InstancePropAccessType::CDataDirectPtr
                           : InstancePropAccessType::CDataDirect;
    current = next;
  }

  const auto *field = root_class->get_instance_field(root->get_string());
  if (field) {
    kphp_assert(field->type_hint);
    const auto *php_type = G->get_ffi_root().c2php_field_type_hint(field->type_hint);
    kphp_error_act(php_type, fmt_format("Unsupported c2php type: {}", field->type_hint->as_human_readable()), return root);
    auto conv = VertexAdaptor<op_ffi_c2php_conv>::create(root).set_location(root);
    conv->php_type = php_type;
    return conv;
  }
  // for now we only support var->cdata; PHP allows nested cdata write, but not read
  //   $struct->field->cdata = $value;  // OK in PHP
  //   var_dump($struct->field->cdata); // Error in PHP
  // both ->cdata forms give compile-time error in KPHP as this style of access is redundant
  //   $struct->field = $value;  // works in both PHP and KPHP
  //   var_dump($struct->field); // works in both PHP and KPHP
  if (root->instance()->type() == op_var && root->get_string() == "cdata") {
    const auto *php_type = G->get_ffi_root().c2php_scalar_type_hint(root_class);
    if (php_type) {
      auto getref = VertexAdaptor<op_ffi_cdata_value_ref>::create(root->instance()).set_location(root);
      auto conv = VertexAdaptor<op_ffi_c2php_conv>::create(getref).set_location(getref);
      conv->php_type = php_type;
      return conv;
    }
  }

  return root;
}

VertexPtr PreprocessFFIOperationsEnd::on_scope_instance_prop(ClassPtr root_class, VertexAdaptor<op_instance_prop> root) {
  auto *scope = root_class->ffi_scope_mixin;

  auto enum_constant = scope->enum_constants.find(root->get_string());
  if (enum_constant != scope->enum_constants.end()) {
    return GenTree::create_int_const(enum_constant->second);
  }

  root->access_type = InstancePropAccessType::ExternVar;

  const auto *field = root_class->get_instance_field(root->get_string());
  if (field) {
    kphp_assert(field->type_hint);
    const auto *php_type = G->get_ffi_root().c2php_field_type_hint(field->type_hint);
    kphp_error_act(php_type, fmt_format("Unsupported c2php type: {}", field->type_hint->as_human_readable()), return root);
    auto conv = VertexAdaptor<op_ffi_c2php_conv>::create(root).set_location(root);
    conv->php_type = php_type;
    return conv;
  }

  return root;
}

VertexPtr PreprocessFFIOperationsEnd::on_enter_vertex(VertexPtr root) {
  if (auto instance_prop = root.try_as<op_instance_prop>()) {
    if (instance_prop->access_type != InstancePropAccessType::Default) {
      // not a first time we touch this instance_prop, skip it
      // as it was processed through its parent
      return root;
    }

    ClassPtr root_class = infer_class_of_expr(current_function, instance_prop->instance()).try_as_class();
    if (!root_class) {
      return root;
    }
    if (root_class->is_ffi_scope()) {
      return on_scope_instance_prop(root_class, instance_prop);
    }
    if (root_class->is_ffi_cdata()) {
      return on_cdata_instance_prop(root_class, instance_prop);
    }
  }

  return root;
}

VertexPtr PreprocessFFIOperationsEnd::on_exit_vertex(VertexPtr root) {
  if (auto call = root.try_as<op_func_call>()) {
    if (call->extra_type != op_ex_func_call_arrow) {
      const std::string func_name = get_full_static_member_name(current_function, call->get_string());

      if (func_name == "count" && call->args().size() == 1) {
        if (auto c2php_conv = call->args()[0].try_as<op_ffi_c2php_conv>()) {
          call->args()[0] = c2php_conv->expr();
        }
      }

      // FFI::isNull(c2php($x)) -> FFI::isNull($x)
      if (func_name == "FFI$$isNull" && call->args().size() == 1) {
        if (auto c2php_conv = call->args()[0].try_as<op_ffi_c2php_conv>()) {
          call->args()[0] = c2php_conv->expr();
        }
      }

      // some CData-like types are not boxed, but we want get_class()
      // to work in the same way as in PHP; so we fold all
      // get_class() over cdata types to the result we expect
      if (func_name == "get_class" && call->args().size() == 1) {
        if (get_cdata_class(current_function, call->args()[0])) {
          auto class_name = VertexAdaptor<op_string>::create().set_location(call);
          class_name->str_val = "FFI\\CData";
          return class_name;
        }
      }
      return root;
    }
    ClassPtr klass = infer_class_of_expr(current_function, call->args()[0]).try_as_class();
    if (klass && klass->is_ffi_scope()) {
      const auto *method = klass->get_instance_method(call->get_string());
      if (!method) {
        return root;
      }
      FunctionPtr func = method->function;
      if (func->get_params().size() != call->args().size()) {
        if (!func->has_variadic_param) {
          return root;
        }
      }
      for (int i = 1; i < call->args().size(); i++) {
        auto arg = call->args()[i];

        // if it's c2php(), we don't want to insert php2c() for this
        // argument as it would be a (potentially expensive) no-op php2c(c2php(arg))
        if (auto c2php_conv = arg.try_as<op_ffi_c2php_conv>()) {
          call->args()[i] = c2php_conv->expr();
          continue;
        }

        auto php2c_conv = VertexAdaptor<op_ffi_php2c_conv>::create(call->args()[i]).set_location(call->args()[i]);
        if (func->has_variadic_param && i >= func->get_params().size() - 1) {
          php2c_conv->c_type = TypeHintInstance::create("FFI\\CData");
        } else {
          php2c_conv->c_type = func->get_params()[i].as<op_func_param>()->type_hint;
        }
        call->args()[i] = php2c_conv;
      }
      const auto *php_type = G->get_ffi_root().c2php_return_type_hint(func->return_typehint);
      kphp_error_act(php_type, fmt_format("Unsupported c2php type: {}", func->return_typehint->as_human_readable()), return root);
      auto conv = VertexAdaptor<op_ffi_c2php_conv>::create(call).set_location(call);
      conv->php_type = php_type;
      return conv;
    }
  }

  // we don't really support list assignments into cdata,
  // but we remove excessive c2php conv vertices to give better error messages
  if (auto list = root.try_as<op_list>()) {
    for (auto x : list->list()) {
      auto kv = x.as<op_list_keyval>();
      if (auto c2php_conv = kv->var().try_as<op_ffi_c2php_conv>()) {
        kv->var() = c2php_conv->expr();
      }
    }
  }

  if (auto assign = root.try_as<op_set_modify>()) {
    if (auto c2php_conv = assign->lhs().try_as<op_ffi_c2php_conv>()) {
      auto lhs = c2php_conv->expr();

      if (auto rhs_c2php_conv = assign->rhs().try_as<op_ffi_c2php_conv>()) {
        assign->rhs() = rhs_c2php_conv->expr();
      } else {
        auto php2c_conv = VertexAdaptor<op_ffi_php2c_conv>::create(assign->rhs()).set_location(assign->rhs());
        const TypeHint *c_type = infer_class_of_expr(current_function, lhs).assum_hint;
        php2c_conv->c_type = c_type;
        php2c_conv->simple_dst = lhs->type() == op_var;
        assign->rhs() = php2c_conv;
      }
      assign->lhs() = lhs;
      return assign;
    }
  }

  // FFI::addr(c2php($x)) -> FFI::addr($x)
  // FFI::cast(c2php($x)) -> FFI::cast($x)
  if (vk::any_of_equal(root->type(), op_ffi_addr, op_ffi_cast)) {
    auto ffi_unary = root.as<meta_op_unary>();
    if (auto c2php_conv = ffi_unary->expr().try_as<op_ffi_c2php_conv>()) {
      ffi_unary->expr() = c2php_conv->expr();
      return ffi_unary;
    }
  }

  return root;
}
