#pragma once

#include "compiler/class-assumptions.h"
#include "compiler/class-members.h"
#include "compiler/data/function-info.h"
#include "compiler/data_ptr.h"
#include "compiler/stage.h"
#include "compiler/type-inferer-core.h"
#include "compiler/vertex-meta_op_base.h"

class FunctionData {
public:
  int id;

  string name;        // полное имя функции, в случае принадлежности классу это VK$Namespace$funcname
  VertexPtr root;     // op_function
  VertexPtr header;   // это только для костыля extern_function, потом должно уйти
  bool is_required;

  enum func_type_t {
    func_global,
    func_local,
    func_switch,
    func_extern
  };
  func_type_t type_;

  vector<VarPtr> local_var_ids, global_var_ids, static_var_ids, header_global_var_ids;
  vector<VarPtr> tmp_vars;
  vector<VarPtr> *bad_vars;
  vector<DefinePtr> define_ids;
  set<VarPtr> const_var_ids, header_const_var_ids;
  vector<VarPtr> param_ids;
  vector<FunctionPtr> dep;

  std::vector<Assumption> assumptions_for_vars;
  Assumption assumption_for_return;
  int assumptions_inited_args;
  int assumptions_inited_return;

  string src_name, header_name;
  string subdir;
  string header_full_name;
  SrcFilePtr file_id;
  FunctionPtr fork_prev, wait_prev;
  ClassPtr class_id;
  bool varg_flag;

  int tinf_state;
  vector<tinf::VarNode> tinf_nodes;

  VertexPtr const_data;
  Token *phpdoc_token;

  int min_argn;
  bool is_extern;
  bool used_in_source;    // это только для костыля extern_function, потом должно уйти
  bool is_callback;
  bool should_be_sync;
  bool kphp_required;
  bool is_template;
  string namespace_name;
  string class_context_name;
  AccessType access_type;
  set<string> disabled_warnings;
  map<long long, int> name_gen_map;

  FunctionData();
  explicit FunctionData(VertexPtr root);
  static FunctionPtr create_function(const FunctionInfo &info);

  inline func_type_t &type() { return type_; }

  bool is_static_init_empty_body() const;
  string get_resumable_path() const;
  string get_human_readable_name() const;

  inline bool is_instance_function() const {
    return access_type == access_public || access_type == access_protected || access_type == access_private;
  }

  inline bool is_static_function() const {
    return access_type == access_static_public || access_type == access_static_protected || access_type == access_static_private;
  }

  bool is_constructor() const;

  static FunctionPtr generate_instance_of_template_function(const std::map<int, std::pair<AssumType, ClassPtr>> &template_type_id_to_ClassPtr,
                                                            FunctionPtr func,
                                                            const std::string &name_of_function_instance);

  static ClassPtr is_lambda(VertexPtr v);

  static const std::string &get_lambda_namespace() {
    static std::string lambda_namespace("$L");
    return lambda_namespace;
  }

  bool is_lambda() const {
    return !!function_in_which_lambda_was_created;
  }

  const std::string get_outer_namespace_name() const {
    return get_or_default_field(&FunctionData::namespace_name);
  }

  ClassPtr get_outer_class() const {
    return is_lambda() ? function_in_which_lambda_was_created->class_id : class_id;
  }

  const std::string &get_outer_class_context_name() const {
    return get_or_default_field(&FunctionData::class_context_name);
  }

  void set_function_in_which_lambda_was_created(FunctionPtr f) {
    function_in_which_lambda_was_created = f;
  }

  VertexRange get_params();

private:
  const std::string &get_or_default_field(std::string (FunctionData::*field)) const {
    if (is_lambda()) {
      kphp_assert(function_in_which_lambda_was_created);
      return function_in_which_lambda_was_created->get_or_default_field(field);
    }

    return this->*field;
  }

  DISALLOW_COPY_AND_ASSIGN (FunctionData);

private:
  FunctionPtr function_in_which_lambda_was_created;
};
