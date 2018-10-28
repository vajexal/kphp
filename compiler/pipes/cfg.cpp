#include "compiler/pipes/cfg.h"

#include "compiler/gentree.h"
#include "compiler/utils/dsu.h"
#include "compiler/utils/idgen.h"

class CFGData {
private:
  FunctionPtr function;

  vector<VertexPtr> uninited_vars;
  vector<VarPtr> todo_var;
  vector<vector<vector<VertexPtr>>> todo_parts;
public:
  void set_function(FunctionPtr new_function) {
    function = new_function;
  }

  void split_var(VarPtr var, vector<vector<VertexPtr>> &parts) {
    assert (var->type() == VarData::var_local_t || var->type() == VarData::var_param_t);
    int parts_size = (int)parts.size();
    if (parts_size == 0) {
      if (var->type() == VarData::var_local_t) {
        function->local_var_ids.erase(
          std::find(
            function->local_var_ids.begin(),
            function->local_var_ids.end(),
            var));
      }
      return;
    }
    assert (parts_size > 1);

    for (int i = 0; i < parts_size; i++) {
      string new_name = var->name + "$v_" + int_to_str(i);
      VarPtr new_var = G->create_var(new_name, var->type());

      for (int j = 0; j < (int)parts[i].size(); j++) {
        VertexPtr v = parts[i][j];
        v->set_var_id(new_var);
      }

      VertexRange params = function->root.
        as<meta_op_function>()->params().
                                     as<op_func_param_list>()->args();
      if (var->type() == VarData::var_local_t) {
        new_var->type() = VarData::var_local_t;
        function->local_var_ids.push_back(new_var);
      } else if (var->type() == VarData::var_param_t) {
        bool was_var = std::find(
          parts[i].begin(),
          parts[i].end(),
          params[var->param_i].as<op_func_param>()->var()
        ) != parts[i].end();

        if (was_var) { //union of part that contains function argument
          new_var->type() = VarData::var_param_t;
          new_var->param_i = var->param_i;
          new_var->init_val = var->init_val;
          function->param_ids[var->param_i] = new_var;
        } else {
          new_var->type() = VarData::var_local_t;
          function->local_var_ids.push_back(new_var);
        }
      } else {
        kphp_fail();
      }

    }

    if (var->type() == VarData::var_local_t) {
      vector<VarPtr>::iterator tmp = std::find(function->local_var_ids.begin(), function->local_var_ids.end(), var);
      if (function->local_var_ids.end() != tmp) {
        function->local_var_ids.erase(tmp);
      } else {
        kphp_fail();
      }
    }

    todo_var.push_back(var);

    //it could be simple std::move
    todo_parts.push_back(vector<vector<VertexPtr>>());
    std::swap(todo_parts.back(), parts);
  }

  void unused_vertices(vector<VertexPtr *> &v) {
    for (auto i: v) {
      auto empty = VertexAdaptor<op_empty>::create();
      *i = empty;
    }
  }

  FunctionPtr get_function() {
    return function;
  }

  void uninited(VertexPtr v) {
    if (v && v->type() == op_var && v->extra_type != op_ex_var_superlocal && v->extra_type != op_ex_var_this) {
      uninited_vars.push_back(v);
      v->get_var_id()->set_uninited_flag(true);
    }
  }

  void check_uninited() {
    for (int i = 0; i < (int)uninited_vars.size(); i++) {
      VertexPtr v = uninited_vars[i];
      VarPtr var = v->get_var_id();
      if (tinf::get_type(v)->ptype() == tp_var) {
        continue;
      }

      stage::set_location(v->get_location());
      kphp_warning (dl_pstr("Variable [%s] may be used uninitialized", var->name.c_str()));
    }
  }

  VarPtr merge_vars(vector<VarPtr> vars, const string &new_name) {
    VarPtr new_var = G->create_var(new_name, VarData::var_unknown_t);;
    //new_var->tinf = vars[0]->tinf; //hack, TODO: fix it
    new_var->tinf_node.copy_type_from(tinf::get_type(vars[0]));

    int param_i = -1;
    for (VarPtr var : vars) {
      if (var->type() == VarData::var_param_t) {
        param_i = var->param_i;
      } else if (var->type() == VarData::var_local_t) {
        //FIXME: remember to remove all unused variables
        //func->local_var_ids.erase (*i);
        vector<VarPtr>::iterator tmp = std::find(function->local_var_ids.begin(), function->local_var_ids.end(), var);
        if (function->local_var_ids.end() != tmp) {
          function->local_var_ids.erase(tmp);
        } else {
          kphp_fail();
        }

      } else {
        assert (0 && "unreachable");
      }
    }
    if (param_i != -1) {
      new_var->type() = VarData::var_param_t;
      function->param_ids[param_i] = new_var;
    } else {
      new_var->type() = VarData::var_local_t;
      function->local_var_ids.push_back(new_var);
    }

    return new_var;
  }


  struct MergeData {
    int id;
    VarPtr var;

    MergeData(int id, VarPtr var) :
      id(id),
      var(var) {
    }
  };

  static bool cmp_merge_data(const MergeData &a, const MergeData &b) {
    return type_out(tinf::get_type(a.var)) <
           type_out(tinf::get_type(b.var));
  }

  static bool eq_merge_data(const MergeData &a, const MergeData &b) {
    return type_out(tinf::get_type(a.var)) ==
           type_out(tinf::get_type(b.var));
  }

  void merge_same_type() {
    int todo_n = (int)todo_parts.size();
    for (int todo_i = 0; todo_i < todo_n; todo_i++) {
      vector<vector<VertexPtr>> &parts = todo_parts[todo_i];

      int n = (int)parts.size();
      vector<MergeData> to_merge;
      for (int i = 0; i < n; i++) {
        to_merge.push_back(MergeData(i, parts[i][0]->get_var_id()));
      }
      sort(to_merge.begin(), to_merge.end(), cmp_merge_data);

      vector<int> ids;
      int merge_id = 0;
      for (int i = 0; i <= n; i++) {
        if (i == n || (i > 0 && !eq_merge_data(to_merge[i - 1], to_merge[i]))) {
          vector<VarPtr> vars;
          for (int id : ids) {
            vars.push_back(parts[id][0]->get_var_id());
          }
          string new_name = vars[0]->name;
          int name_i = (int)new_name.size() - 1;
          while (new_name[name_i] != '$') {
            name_i--;
          }
          new_name.erase(name_i);
          new_name += "$v";
          new_name += int_to_str(merge_id++);

          VarPtr new_var = merge_vars(vars, new_name);
          for (int id : ids) {
            for (VertexPtr v : parts[id]) {
              v->set_var_id(new_var);
            }
          }

          ids.clear();
        }
        if (i == n) {
          break;
        }
        ids.push_back(to_merge[i].id);
      }
    }
  }
};

namespace cfg {
//just simple int id type
struct IdBase {
  int id;
  IdBase();
};

typedef Id<IdBase> Node;
typedef vector<Node> NodesList;

enum UsageType {
  usage_write_t,
  usage_read_t
};

struct UsageData {
  int id;
  int part_id;
  UsageType type;
  bool weak_write_flag;
  VertexPtr v;
  Node node;
  explicit UsageData(UsageType type, VertexPtr v);
};

typedef Id<UsageData> UsagePtr;

struct SubTreeData {
  VertexPtr v;
  bool recursive_flag;
  SubTreeData(VertexPtr v, bool recursive_flag);
};

typedef Id<SubTreeData> SubTreePtr;

struct VertexUsage {
  bool used;
  bool used_rec;
  VertexUsage();
};

struct VarSplitData {
  int n;

  IdGen<UsagePtr> usage_gen;
  IdMap<UsagePtr> parent;

  VarSplitData();
};

typedef Id<VarSplitData> VarSplitPtr;

class CFG {
  CFGData *data;
  FunctionPtr cur_function;
  IdGen<Node> node_gen;
  IdMap<vector<Node>> node_next, node_prev;
  IdMap<vector<UsagePtr>> node_usages;
  IdMap<vector<SubTreePtr>> node_subtrees;
  IdMap<VertexUsage> vertex_usage;
  int cur_dfs_mark;
  Node current_start;
  Node current_finish;

  IdMap<int> node_was;
  IdMap<UsagePtr> node_mark;
  IdMap<VarSplitPtr> var_split_data;

  vector<vector<Node>> continue_nodes;
  vector<vector<Node>> break_nodes;
  void create_cfg_enter_cycle();
  void create_cfg_exit_cycle(Node continue_dest, Node break_dest);
  void create_cfg_add_break_node(Node v, int depth);
  void create_cfg_add_continue_node(Node v, int depth);

  vector<vector<Node>> exception_nodes;
  void create_cfg_begin_try();
  void create_cfg_end_try(Node to);
  void create_cfg_register_exception(Node from);

  VarSplitPtr get_var_split(VarPtr var, bool force);
  Node new_node();
  UsagePtr new_usage(UsageType type, VertexPtr v);
  void add_usage(Node node, UsagePtr usage);
  SubTreePtr new_subtree(VertexPtr v, bool recursive_flag);
  void add_subtree(Node node, SubTreePtr subtree);
  void add_edge(Node from, Node to);
  void collect_ref_vars(VertexPtr v, set<VarPtr> *ref);
  void find_splittable_vars(FunctionPtr func, vector<VarPtr> *splittable_vars);
  void collect_vars_usage(VertexPtr tree_node, Node writes, Node reads, bool *throws_flag);
  void create_full_cfg(VertexPtr tree_node, Node *res_start, Node *res_finish);
  void create_cfg(VertexPtr tree_node, Node *res_start, Node *res_finish,
                  bool set_flag = false, bool weak_write_flag = false);
  void create_condition_cfg(VertexPtr tree_node, Node *res_start, Node *res_true, Node *res_false);

  void calc_used(Node v);
  void confirm_usage(VertexPtr, bool recursive_flags);
  void collect_unused(VertexPtr *v, vector<VertexPtr *> *unused_vertices);

  UsagePtr search_uninited(Node v, VarPtr var);

  bool try_uni_usages(UsagePtr usage, UsagePtr another_usage);
  void compress_usages(vector<UsagePtr> *usages);
  void dfs(Node v, UsagePtr usage);
  void process_var(VarPtr v);
  void process_node(Node v);
  int register_vertices(VertexPtr v, int N);
  void process_function(FunctionPtr func);
public:
  void run(CFGData *new_data);
};

//just simple int id type
IdBase::IdBase() :
  id(-1) {
}

UsageData::UsageData(UsageType type, VertexPtr v) :
  id(-1),
  part_id(-1),
  type(type),
  weak_write_flag(false),
  v(v),
  node() {
}

SubTreeData::SubTreeData(VertexPtr v, bool recursive_flag) :
  v(v),
  recursive_flag(recursive_flag) {
}

VertexUsage::VertexUsage() :
  used(false),
  used_rec(false) {
}

VarSplitData::VarSplitData() :
  n(0) {
  usage_gen.add_id_map(&parent);
}

VarSplitPtr CFG::get_var_split(VarPtr var, bool force) {
  if (get_index(var) < 0) {
    return VarSplitPtr();
  }
  VarSplitPtr res = var_split_data[var];
  if (!res && force) {
    res = VarSplitPtr(new VarSplitData());
    var_split_data[var] = res;
  }
  return res;
}

Node CFG::new_node() {
  Node res = Node(new IdBase());
  node_gen.init_id(&res);
  return res;
}

UsagePtr CFG::new_usage(UsageType type, VertexPtr v) {
  VarPtr var = v->get_var_id();
  kphp_assert (var);
  VarSplitPtr var_split = get_var_split(var, false);
  if (!var_split) {
    return UsagePtr();
  }
  UsagePtr res = UsagePtr(new UsageData(type, v));
  var_split->usage_gen.init_id(&res);
  var_split->parent[res] = res;
  return res;
}

void CFG::add_usage(Node node, UsagePtr usage) {
  if (!usage) {
    return;
  }
  //hope that one node will contain usages of the same type
  kphp_assert (node_usages[node].empty() || node_usages[node].back()->type == usage->type);
  node_usages[node].push_back(usage);
  usage->node = node;

//    VertexPtr v = usage->v; //TODO assigned but not used
}

SubTreePtr CFG::new_subtree(VertexPtr v, bool recursive_flag) {
  SubTreePtr res = SubTreePtr(new SubTreeData(v, recursive_flag));
  return res;
}

void CFG::add_subtree(Node node, SubTreePtr subtree) {
  kphp_assert (node && subtree);
  node_subtrees[node].push_back(subtree);
}

void CFG::add_edge(Node from, Node to) {
  if (from && to) {
    // fprintf(stderr, "%s, add-edge: %d->%d\n", stage::get_function_name().c_str(), from->id, to->id);
    node_next[from].push_back(to);
    node_prev[to].push_back(from);
  }
}

void CFG::collect_ref_vars(VertexPtr v, set<VarPtr> *ref) {
  if (v->type() == op_var && v->ref_flag) {
    ref->insert(v->get_var_id());
  }
  for (auto i : *v) {
    collect_ref_vars(i, ref);
  }
}

struct XPred {
  set<VarPtr> *ref;

  XPred(set<VarPtr> *ref) :
    ref(ref) {};

  bool operator()(const VarPtr &x) { return ref->count(x); }
};

void CFG::find_splittable_vars(FunctionPtr func, vector<VarPtr> *splittable_vars) {
  splittable_vars->insert(splittable_vars->end(), func->local_var_ids.begin(), func->local_var_ids.end());
  VertexAdaptor<meta_op_function> func_root = func->root;
  VertexAdaptor<op_func_param_list> params = func_root->params();
  for (VarPtr var : func->param_ids) {
    VertexAdaptor<op_func_param> param = params->params()[var->param_i];
    VertexPtr init = param->var();
    kphp_assert (init->type() == op_var);
    if (!init->ref_flag) {
      splittable_vars->push_back(var);
    }
  }

  //todo: references in foreach
  set<VarPtr> ref;
  collect_ref_vars(func->root, &ref);
  splittable_vars->erase(std::remove_if(splittable_vars->begin(), splittable_vars->end(), XPred(&ref)),
                         splittable_vars->end());
}

void CFG::collect_vars_usage(VertexPtr tree_node, Node writes, Node reads, bool *throws_flag) {
  //TODO: a lot of problems
  //is_set, unset, reference arguments...

  if (tree_node->type() == op_throw) {
    *throws_flag = true;
  }
  //TODO: only if function has throws flag
  if (tree_node->type() == op_func_call) {
    *throws_flag = tree_node->get_func_id()->root->throws_flag;
  }

  if (tree_node->type() == op_set) {
    VertexAdaptor<op_set> set_op = tree_node;
    if (set_op->lhs()->type() == op_var) {
      add_usage(writes, new_usage(usage_write_t, set_op->lhs()));
      collect_vars_usage(set_op->rhs(), writes, reads, throws_flag);
      return;
    }
  }
  if (tree_node->type() == op_var) {
    add_usage(reads, new_usage(usage_read_t, tree_node));
  }
  for (auto i : *tree_node) {
    collect_vars_usage(i, writes, reads, throws_flag);
  }
}

void CFG::create_cfg_enter_cycle() {
  continue_nodes.resize(continue_nodes.size() + 1);
  break_nodes.resize(break_nodes.size() + 1);
}

void CFG::create_cfg_exit_cycle(Node continue_dest, Node break_dest) {
  for (Node i : continue_nodes.back()) {
    add_edge(i, continue_dest);
  }
  for (Node i : break_nodes.back()) {
    add_edge(i, break_dest);
  }
  continue_nodes.pop_back();
  break_nodes.pop_back();
}

void CFG::create_cfg_add_break_node(Node v, int depth) {
  kphp_assert (depth >= 1);
  int i = (int)break_nodes.size() - depth;
  kphp_assert (i >= 0);
  break_nodes[i].push_back(v);
}

void CFG::create_cfg_add_continue_node(Node v, int depth) {
  kphp_assert (depth >= 1);
  int i = (int)continue_nodes.size() - depth;
  kphp_assert (i >= 0);
  continue_nodes[i].push_back(v);
}

void CFG::create_cfg_begin_try() {
  exception_nodes.resize(exception_nodes.size() + 1);
}

void CFG::create_cfg_end_try(Node to) {
  for (Node i : exception_nodes.back()) {
    add_edge(i, to);
  }
  exception_nodes.pop_back();
}

void CFG::create_cfg_register_exception(Node from) {
  if (exception_nodes.empty()) {
    return;
  }
  exception_nodes.back().push_back(from);
}

void CFG::create_full_cfg(VertexPtr tree_node, Node *res_start, Node *res_finish) {
  stage::set_location(tree_node->location);
  Node start = new_node(),
    finish = new_node(),
    writes = new_node(),
    reads = new_node();

  bool throws_flag = false;
  collect_vars_usage(tree_node, writes, reads, &throws_flag);
  compress_usages(&node_usages[writes]);
  compress_usages(&node_usages[reads]);

  add_subtree(start, new_subtree(tree_node, true));

  //add_edge (start, finish);
  add_edge(start, writes);
  add_edge(start, reads);
  add_edge(writes, reads);
  add_edge(writes, finish);
  add_edge(reads, finish);
  //TODO: (reads->writes) (finish->start)

  *res_start = start;
  *res_finish = finish;
  if (throws_flag) {
    create_cfg_register_exception(*res_finish);
  }
}

void CFG::create_condition_cfg(VertexPtr tree_node, Node *res_start, Node *res_true, Node *res_false) {
  switch (tree_node->type()) {
    case op_conv_bool: {
      create_condition_cfg(tree_node.as<op_conv_bool>()->expr(), res_start, res_true, res_false);
      break;
    }
    case op_log_not: {
      create_condition_cfg(tree_node.as<op_log_not>()->expr(), res_start, res_false, res_true);
      break;
    }
    case op_log_and:
    case op_log_or: {
      Node first_start, first_true, first_false, second_start, second_true, second_false;
      VertexAdaptor<meta_op_binary_op> op = tree_node;
      create_condition_cfg(op->lhs(), &first_start, &first_true, &first_false);
      create_condition_cfg(op->rhs(), &second_start, &second_true, &second_false);
      *res_start = first_start;
      *res_true = new_node();
      *res_false = new_node();
      add_edge(first_true, tree_node->type() == op_log_and ? second_start : *res_true);
      add_edge(first_false, tree_node->type() == op_log_or ? second_start : *res_false);
      add_edge(second_true, *res_true);
      add_edge(second_false, *res_false);
      break;
    }
    default: {
      Node res_finish;
      create_cfg(tree_node, res_start, &res_finish);
      *res_true = new_node();
      *res_false = new_node();
      add_edge(res_finish, *res_true);
      add_edge(res_finish, *res_false);
      break;
    }
  }

  add_subtree(*res_start, new_subtree(tree_node, false));
}


void CFG::create_cfg(VertexPtr tree_node, Node *res_start, Node *res_finish, bool write_flag, bool weak_write_flag) {
  stage::set_location(tree_node->location);
  bool recursive_flag = false;
  switch (tree_node->type()) {
    case op_min:
    case op_max:
    case op_array:
    case op_tuple:
    case op_seq_comma:
    case op_seq_rval:
    case op_seq: {
      Node a, b, end;
      if (tree_node->empty()) {
        a = new_node();
        *res_start = a;
        *res_finish = a;
        break;
      }
      VertexRange args = tree_node.as<meta_op_varg_>()->args();
      create_cfg(args[0], res_start, &b);
      end = b;
      for (int i = 1; i < (int)tree_node->size(); i++) {
        create_cfg(args[i], &a, &b);
        add_edge(end, a);
        end = b;
      }
      *res_finish = end;
      break;
    }
    case op_log_not: {
      create_cfg(tree_node.as<op_log_not>()->expr(), res_start, res_finish);
      break;
    }
    case op_neq3:
    case op_eq3:
    case op_eq2:
    case op_neq2: {
      VertexAdaptor<meta_op_binary_op> op = tree_node;
      if (op->rhs()->type() == op_false || op->rhs()->type() == op_null) {
        Node first_start, first_finish, second_start;
        create_cfg(op->lhs(), res_start, &first_finish);
        create_cfg(op->rhs(), &second_start, res_finish);
        add_edge(first_finish, second_start);
      } else {
        create_full_cfg(tree_node, res_start, res_finish);
      }
      break;
    }
    case op_index: {
      Node var_start, var_finish;
      VertexAdaptor<op_index> index = tree_node;
      create_cfg(index->array(), &var_start, &var_finish, false, write_flag || weak_write_flag);
      Node start = var_start;
      Node finish = var_finish;
      if (index->has_key()) {
        Node index_start, index_finish;
        create_cfg(index->key(), &index_start, &index_finish);
        add_edge(index_finish, start);
        start = index_start;
      }
      *res_start = start;
      *res_finish = finish;
      break;
    }
    case op_log_and:
    case op_log_or: {
      Node first_start, first_finish, second_start, second_finish;
      VertexAdaptor<meta_op_binary_op> op = tree_node;
      create_cfg(op->lhs(), &first_start, &first_finish);
      create_cfg(op->rhs(), &second_start, &second_finish);
      Node finish = new_node();
      add_edge(first_finish, second_start);
      add_edge(second_finish, finish);
      add_edge(first_finish, finish);
      *res_start = first_start;
      *res_finish = finish;
      break;
    }
    case op_func_call:
    case op_constructor_call: {
      FunctionPtr func = tree_node->get_func_id();

      Node start, a, b;
      start = new_node();
      *res_start = start;

      int ii = 0;
      for (auto cur : tree_node.as<op_func_call>()->args()) {
        bool new_weak_write_flag = false;

        if (func && !func->varg_flag) {
          auto params = func->root.as<meta_op_function>()->params().as<op_func_param_list>()->params();
          if (params[ii]->type() == op_func_param && params[ii].as<op_func_param>()->var()->ref_flag) {
            new_weak_write_flag = true;
          }
        }

        kphp_assert (cur);
        create_cfg(cur, &a, &b, false, new_weak_write_flag);
        add_edge(start, a);
        start = b;

        ii++;
      }
      *res_finish = start;

      //if function has throws flag
      if (func->root->throws_flag) {
        create_cfg_register_exception(*res_finish);
      }
      break;
    }
    case op_return: {
      VertexAdaptor<op_return> return_op = tree_node;
      Node tmp;

      create_cfg(return_op->expr(), res_start, &tmp);
      *res_finish = Node();
      break;
    }
    case op_set: {
      VertexAdaptor<op_set> set_op = tree_node;
      Node a, b;
      create_cfg(set_op->rhs(), res_start, &a);
      create_cfg(set_op->lhs(), &b, res_finish, true);
      add_edge(a, b);
      break;
    }
    case op_set_add:
    case op_set_sub:
    case op_set_mul:
    case op_set_div:
    case op_set_mod:
    case op_set_and:
    case op_set_or:
    case op_set_xor:
    case op_set_dot:
    case op_set_shr:
    case op_set_shl: {
      VertexAdaptor<meta_op_binary_op> set_op = tree_node;
      Node a, b;
      create_cfg(set_op->rhs(), res_start, &a);
      create_full_cfg(set_op->lhs(), &b, res_finish);
      add_edge(a, b);
      break;
    }
    case op_list: {
      VertexAdaptor<op_list> list = tree_node;
      Node prev;
      create_cfg(list->array(), res_start, &prev);
      for (auto param : list->list().get_reversed_range()) {
        Node a, b;
        create_cfg(param, &a, &b, true);
        add_edge(prev, a);
        prev = b;
      }
      *res_finish = prev;
      break;
    }
    case op_var: {
      Node res = new_node();
      UsagePtr usage = new_usage(write_flag ? usage_write_t : usage_read_t, tree_node);
      if (usage) {
        usage->weak_write_flag = weak_write_flag;
      }
      add_usage(res, usage);
      *res_start = *res_finish = res;
      break;
    }
    case op_if: {
      VertexAdaptor<op_if> if_op = tree_node;
      Node finish = new_node();
      Node cond_true, cond_false, if_start, if_finish;
      create_condition_cfg(if_op->cond(), res_start, &cond_true, &cond_false);
      create_cfg(if_op->true_cmd(), &if_start, &if_finish);
      add_edge(cond_true, if_start);
      add_edge(if_finish, finish);
      if (if_op->has_false_cmd()) {
        Node else_start, else_finish;
        create_cfg(if_op->false_cmd(), &else_start, &else_finish);
        add_edge(cond_false, else_start);
        add_edge(else_finish, finish);
      } else {
        add_edge(cond_false, finish);
      }

      *res_finish = finish;
      break;
    }
    case op_ternary: {
      VertexAdaptor<op_ternary> ternary_op = tree_node;
      Node finish = new_node();
      Node cond_true, cond_false;

      Node if_start, if_finish;
      create_condition_cfg(ternary_op->cond(), res_start, &cond_true, &cond_false);
      create_cfg(ternary_op->true_expr(), &if_start, &if_finish);
      add_edge(cond_true, if_start);
      add_edge(if_finish, finish);

      Node else_start, else_finish;
      create_cfg(ternary_op->false_expr(), &else_start, &else_finish);
      add_edge(cond_false, else_start);
      add_edge(else_finish, finish);

      *res_finish = finish;
      break;
    }
    case op_break: {
      VertexAdaptor<op_break> break_op = tree_node;
      recursive_flag = true;
      Node start = new_node(), finish = Node();
      create_cfg_add_break_node(start, atoi(break_op->expr()->get_string().c_str()));

      *res_start = start;
      *res_finish = finish;
      break;
    }
    case op_continue: {
      VertexAdaptor<op_continue> continue_op = tree_node;
      recursive_flag = true;
      Node start = new_node(), finish = Node();
      create_cfg_add_continue_node(start, atoi(continue_op->expr()->get_string().c_str()));

      *res_start = start;
      *res_finish = finish;
      break;
    }
    case op_for: {
      create_cfg_enter_cycle();

      VertexAdaptor<op_for> for_op = tree_node;

      Node init_start, init_finish;
      create_cfg(for_op->pre_cond(), &init_start, &init_finish);

      Node cond_start, cond_finish_true, cond_finish_false;
      create_condition_cfg(for_op->cond(), &cond_start, &cond_finish_true, &cond_finish_false);

      Node inc_start, inc_finish;
      create_cfg(for_op->post_cond(), &inc_start, &inc_finish);

      Node action_start, action_finish_pre, action_finish = new_node();
      create_cfg(for_op->cmd(), &action_start, &action_finish_pre);
      add_edge(action_finish_pre, action_finish);

      add_edge(init_finish, cond_start);
      add_edge(cond_finish_true, action_start);
      add_edge(action_finish, inc_start);
      add_edge(inc_finish, cond_start);

      Node finish = new_node();
      add_edge(cond_finish_false, finish);

      *res_start = init_start;
      *res_finish = finish;

      create_cfg_exit_cycle(action_finish, finish);
      break;
    }
    case op_do:
    case op_while: {
      create_cfg_enter_cycle();

      VertexPtr cond, cmd;
      if (tree_node->type() == op_do) {
        VertexAdaptor<op_do> do_op = tree_node;
        cond = do_op->cond();
        cmd = do_op->cmd();
      } else if (tree_node->type() == op_while) {
        VertexAdaptor<op_while> while_op = tree_node;
        cond = while_op->cond();
        cmd = while_op->cmd();
      } else {
        kphp_fail();
      }


      Node cond_start, cond_finish_true, cond_finish_false;
      create_condition_cfg(cond, &cond_start, &cond_finish_true, &cond_finish_false);

      Node action_start, action_finish_pre, action_finish = new_node();
      create_cfg(cmd, &action_start, &action_finish_pre);
      add_edge(action_finish_pre, action_finish);

      add_edge(cond_finish_true, action_start);
      add_edge(action_finish, cond_start);

      Node finish = new_node();
      add_edge(cond_finish_false, finish);

      if (tree_node->type() == op_do) {
        *res_start = action_start;
      } else if (tree_node->type() == op_while) {
        *res_start = cond_start;
      } else {
        kphp_fail();
      }
      *res_finish = finish;

      if (tree_node->type() == op_do && action_finish_pre) {
        add_subtree(*res_start, new_subtree(cond, true));
      }

      create_cfg_exit_cycle(action_finish, finish);
      break;
    }
    case op_foreach: {
      create_cfg_enter_cycle();

      VertexAdaptor<op_foreach> foreach_op = tree_node;

      //foreach_param
      VertexAdaptor<op_foreach_param> foreach_param = foreach_op->params();
      Node val_start, val_finish;

      create_cfg(foreach_param->xs(), &val_start, &val_finish);

      Node writes = new_node();
      add_usage(writes, new_usage(usage_write_t, foreach_param->x()));
      if (!foreach_param->x()->ref_flag) {
        add_usage(writes, new_usage(usage_write_t, foreach_param->temp_var()));
      }
      if (foreach_param->has_key()) {
        add_usage(writes, new_usage(usage_write_t, foreach_param->key()));
      }

      //?? not sure
      add_subtree(val_start, new_subtree(foreach_param, true));

      Node finish = new_node();

      Node cond_start = val_start;
      Node cond_check = new_node();
      Node cond_true = writes;
      Node cond_false = finish;

      add_edge(val_finish, cond_check);
      add_edge(cond_check, cond_true);
      add_edge(cond_check, cond_false);

      Node action_start, action_finish_pre, action_finish = new_node();
      create_cfg(foreach_op->cmd(), &action_start, &action_finish_pre);
      add_edge(action_finish_pre, action_finish);

      add_edge(cond_true, action_start);
      add_edge(action_finish, cond_check);

      *res_start = cond_start;
      *res_finish = finish;

      create_cfg_exit_cycle(action_finish, finish);
      break;
    }
    case op_switch: {
      create_cfg_enter_cycle();

      VertexAdaptor<op_switch> switch_op = tree_node;
      Node cond_start, cond_finish;
      create_cfg(switch_op->expr(), &cond_start, &cond_finish);

      Node prev_finish;
      Node prev_var_finish = cond_finish;

      Node vars_init = new_node();
      Node vars_read = new_node();
      {
        add_edge(vars_init, vars_read);
        for (auto i : switch_op->variables()) {
          add_usage(vars_init, new_usage(usage_write_t, i));
          add_usage(vars_read, new_usage(usage_read_t, i));
          add_subtree(vars_init, new_subtree(i, false));
          add_subtree(vars_read, new_subtree(i, false));
        }
      }

      bool was_default = false;
      Node default_start;
      for (auto i : switch_op->cases()) {
        VertexPtr expr, cmd;
        bool is_default = false;
        if (i->type() == op_case) {
          VertexAdaptor<op_case> cs = i;
          expr = cs->expr();
          cmd = cs->cmd();
        } else if (i->type() == op_default) {
          is_default = true;
          VertexAdaptor<op_default> def = i;
          cmd = def->cmd();
        } else {
          kphp_fail();
        }

        Node cur_start, cur_finish;
        create_cfg(cmd, &cur_start, &cur_finish);
        add_edge(prev_finish, cur_start);
        prev_finish = cur_finish;

        Node cur_var_start, cur_var_finish;
        if (is_default) {
          default_start = cur_start;
          was_default = true;
        } else {
          create_cfg(expr, &cur_var_start, &cur_var_finish);
          add_edge(cur_var_finish, cur_start);
          add_edge(prev_var_finish, cur_var_start);
          prev_var_finish = cur_var_finish;
        }
      }
      Node finish = new_node();
      add_edge(prev_finish, finish);
      if (!was_default) {
        add_edge(prev_var_finish, finish);
      }
      if (was_default) {
        add_edge(prev_var_finish, default_start);
      }

      add_edge(vars_read, cond_start);
      *res_start = vars_init;
      *res_finish = finish;

      for (auto i : switch_op->cases()) {
        add_subtree(cond_start, new_subtree(i, false));
      }

      create_cfg_exit_cycle(finish, finish);
      break;
    }
    case op_throw: {
      VertexAdaptor<op_throw> throw_op = tree_node;
      Node throw_start, throw_finish;
      create_cfg(throw_op->expr(), &throw_start, &throw_finish);
      create_cfg_register_exception(throw_finish);

      *res_start = throw_start;
      //*res_finish = throw_finish;
      *res_finish = new_node();
      break;
    }
    case op_try: {
      VertexAdaptor<op_try> try_op = tree_node;
      Node exception_start, exception_finish;
      create_cfg(try_op->exception(), &exception_start, &exception_finish, true);

      Node try_start, try_finish;
      create_cfg_begin_try();
      create_cfg(try_op->try_cmd(), &try_start, &try_finish);
      create_cfg_end_try(exception_start);

      Node catch_start, catch_finish;
      create_cfg(try_op->catch_cmd(), &catch_start, &catch_finish);

      add_edge(exception_finish, catch_start);

      Node finish = new_node();
      add_edge(try_finish, finish);
      add_edge(catch_finish, finish);

      *res_start = try_start;
      *res_finish = finish;

      add_subtree(*res_start, new_subtree(try_op->exception(), false));
      add_subtree(*res_start, new_subtree(try_op->catch_cmd(), true));
      break;
    }

    case op_conv_int:
    case op_conv_int_l:
    case op_conv_float:
    case op_conv_string:
    case op_conv_array:
    case op_conv_array_l:
    case op_conv_object:
    case op_conv_var:
    case op_conv_uint:
    case op_conv_long:
    case op_conv_ulong:
    case op_conv_regexp:
    case op_conv_bool: {
      create_cfg(tree_node.as<meta_op_unary_op>()->expr(), res_start, res_finish);
      break;
    }
    case op_function: {
      VertexAdaptor<op_function> function = tree_node;
      Node a, b;
      create_cfg(function->params(), res_start, &a);
      create_cfg(function->cmd(), &b, res_finish);
      add_edge(a, b);
      break;
    }
    default: {
      create_full_cfg(tree_node, res_start, res_finish);
      return;
    }
  }

  add_subtree(*res_start, new_subtree(tree_node, recursive_flag));
}

bool cmp_by_var_id(const UsagePtr &a, const UsagePtr &b) {
  return a->v->get_var_id() < b->v->get_var_id();
}

bool CFG::try_uni_usages(UsagePtr usage, UsagePtr another_usage) {
  VarPtr var = usage->v->get_var_id();
  VarPtr another_var = another_usage->v->get_var_id();
  if (var == another_var) {
    VarSplitPtr var_split = get_var_split(var, false);
    kphp_assert (var_split);
    dsu_uni(&var_split->parent, usage, another_usage);
    return true;
  }
  return false;
}

void CFG::compress_usages(vector<UsagePtr> *usages) {
  sort(usages->begin(), usages->end(), cmp_by_var_id);
  vector<UsagePtr> res;
  for (int i = 0; i < (int)usages->size(); i++) {
    if (i == 0 || !try_uni_usages((*usages)[i], (*usages)[i - 1])) {
      res.push_back((*usages)[i]);
    } else {
      res.back()->weak_write_flag |= (*usages)[i]->weak_write_flag;
    }
  }
  swap(*usages, res);
}

void CFG::dfs(Node v, UsagePtr usage) {
  UsagePtr other_usage = node_mark[v];
  if (other_usage) {
    try_uni_usages(usage, other_usage);
    return;
  }
  node_mark[v] = usage;

  bool return_flag = false;
  for (UsagePtr another_usage : node_usages[v]) {
    if (try_uni_usages(usage, another_usage) && another_usage->type == usage_write_t) {
      return_flag = true;
    }
  }
  if (return_flag) {
    return;
  }
  for (Node i : node_prev[v]) {
    dfs(i, usage);
  }
}

UsagePtr CFG::search_uninited(Node v, VarPtr var) {
  node_was[v] = cur_dfs_mark;

  bool return_flag = false;
  for (UsagePtr another_usage : node_usages[v]) {
    if (another_usage->v->get_var_id() == var) {
      if (another_usage->type == usage_write_t || another_usage->weak_write_flag) {
        return_flag = true;
      } else if (another_usage->type == usage_read_t) {
        return another_usage;
      }
    }
  }
  if (return_flag) {
    return UsagePtr();
  }

  for (Node i : node_next[v]) {
    if (node_was[i] != cur_dfs_mark) {
      UsagePtr res = search_uninited(i, var);
      if (res) {
        return res;
      }
    }
  }

  return UsagePtr();
}

void CFG::process_var(VarPtr var) {
  VarSplitPtr var_split = get_var_split(var, false);
  kphp_assert (var_split);

  if (var->type() == VarData::var_local_inplace_t) {
    return;
  }
  if (var->type() != VarData::var_param_t) {
    cur_dfs_mark++;
    UsagePtr uninited = search_uninited(current_start, var);
    if (uninited) {
      data->uninited(uninited->v);
    }
  }

  std::fill(node_mark.begin(), node_mark.end(), UsagePtr());

  for (UsagePtr u : var_split->usage_gen) {
    dfs(u->node, u);
  }

  //fprintf (stdout, "PROCESS:[%s][%d]\n", var->name.c_str(), var->id);

  int parts_cnt = 0;
  for (UsagePtr i : var_split->usage_gen) {
    if (node_was[i->node]) {
      UsagePtr u = dsu_get(&var_split->parent, i);
      if (u->part_id == -1) {
        u->part_id = parts_cnt++;
      }
    }
  }

  //printf ("parts_cnt = %d\n", parts_cnt);
  if (parts_cnt == 1) {
    return;
  }

  vector<vector<VertexPtr>> parts(parts_cnt);
  for (UsagePtr i : var_split->usage_gen) {
    if (node_was[i->node]) {
      UsagePtr u = dsu_get(&var_split->parent, i);
      parts[u->part_id].push_back(i->v);
    }
  }

  data->split_var(var, parts);
}

void CFG::confirm_usage(VertexPtr v, bool recursive_flag) {
  //fprintf (stdout, "%s\n", OpInfo::op_str[v->type()].c_str());
  if (!vertex_usage[v].used || (recursive_flag && !vertex_usage[v].used_rec)) {
    vertex_usage[v].used = true;
    if (recursive_flag) {
      vertex_usage[v].used_rec = true;
      for (auto i : *v) {
        confirm_usage(i, true);
      }
    }
  }
}

void CFG::calc_used(Node v) {
  node_was[v] = cur_dfs_mark;
  //fprintf (stdout, "calc_used %d\n", get_index (v));

  for (SubTreePtr node_subtree : node_subtrees[v]) {
    confirm_usage(node_subtree->v, node_subtree->recursive_flag);
  }
  for (Node i : node_next[v]) {
    if (node_was[i] != cur_dfs_mark) {
      calc_used(i);
    }
  }
}

void CFG::collect_unused(VertexPtr *v, vector<VertexPtr *> *unused_vertices) {
  if (!vertex_usage[*v].used) {
    unused_vertices->push_back(v);
    return;
  }
  for (auto &i : **v) {
    collect_unused(&i, unused_vertices);
  }
}

int CFG::register_vertices(VertexPtr v, int N) {
  set_index(v, N++);
  for (auto i : *v) {
    N = register_vertices(i, N);
  }
  return N;
}

void CFG::process_function(FunctionPtr function) {
  //vertex_usage
  //var_split_data

  if (function->type() != FunctionData::func_local) {
    return;
  }

  vector<VarPtr> splittable_vars;
  find_splittable_vars(function, &splittable_vars);

  int var_n = (int)splittable_vars.size();
  var_split_data.update_size(var_n);
  for (int var_i = 0; var_i < var_n; var_i++) {
    VarPtr var = splittable_vars[var_i];
    set_index(var, var_i);
    get_var_split(var, true);
  }

  int vertex_n = register_vertices(function->root, 0);
  vertex_usage.update_size(vertex_n);

  node_gen.add_id_map(&node_next);
  node_gen.add_id_map(&node_prev);
  node_gen.add_id_map(&node_was);
  node_gen.add_id_map(&node_mark);
  node_gen.add_id_map(&node_usages);
  node_gen.add_id_map(&node_subtrees);
  cur_dfs_mark = 0;

  Node start, finish;
  create_cfg(function->root, &start, &finish);
  current_start = start;
  current_finish = finish;

  cur_dfs_mark++;
  calc_used(start);
  vector<VertexPtr *> unused_vertices;
  collect_unused(&function->root.as<op_function>()->cmd(), &unused_vertices);
  data->unused_vertices(unused_vertices);

  std::for_each(splittable_vars.begin(), splittable_vars.end(), std::bind1st(std::mem_fun(&CFG::process_var), this));
  node_gen.clear();
}

void CFG::run(CFGData *new_data) {
  data = new_data;
  process_function(data->get_function());
}
}

void CFGBeginF::execute(FunctionPtr function, DataStream<FunctionAndCFG> &os) {
  AUTO_PROF (CFG);
  stage::set_name("Calc control flow graph");
  stage::set_function(function);

  cfg::CFG cfg;
  CFGData *data = new CFGData();
  data->set_function(function);
  cfg.run(data);

  if (stage::has_error()) {
    return;
  }

  os << FunctionAndCFG(function, data);
}

void CFGEndF::execute(FunctionAndCFG data, DataStream<FunctionPtr> &os) {
  AUTO_PROF (CFG_End);
  stage::set_name("Control flow graph. End");
  stage::set_function(data.function);
  if (G->env().get_warnings_level() >= 1) {
    data.data->check_uninited();
  }
  data.data->merge_same_type();
  delete data.data;

  if (stage::has_error()) {
    return;
  }

  os << data.function;
}
