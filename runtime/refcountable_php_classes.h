// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once
#include <cstdint>

#include "common/php-functions.h"

#include "runtime/allocator.h"

class abstract_refcountable_php_interface : public ManagedThroughDlAllocator {
public:
  abstract_refcountable_php_interface() __attribute__((always_inline)) = default;
  virtual ~abstract_refcountable_php_interface() noexcept __attribute__((always_inline)) = default;
  virtual void add_ref() noexcept = 0;
  virtual void release() noexcept = 0;
  virtual uint32_t get_refcnt() noexcept = 0;
  virtual void set_refcnt(uint32_t new_refcnt) noexcept = 0;

  virtual uint32_t &get_unique_index_ref() noexcept = 0;
};

template<class ...Bases>
class refcountable_polymorphic_php_classes : public Bases... {
public:
  void add_ref() noexcept final {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      ++refcnt_;
    }
  }

  uint32_t get_refcnt() noexcept final {
    return refcnt_;
  }

  void release() noexcept final __attribute__((always_inline)) {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      --refcnt_;
    }
    if (refcnt_ == 0) {
      delete this;
    }
  }

  void set_refcnt(uint32_t new_refcnt) noexcept final {
    refcnt_ = new_refcnt;
  }

  uint32_t &get_unique_index_ref() noexcept final {
    return unique_index_;
  }

private:
  uint32_t refcnt_{0};
  uint32_t unique_index_{0};
};

template<class ...Interfaces>
class refcountable_polymorphic_php_classes_virt : public virtual abstract_refcountable_php_interface, public Interfaces... {
public:
  refcountable_polymorphic_php_classes_virt() __attribute__((always_inline)) = default;
};

template<>
class refcountable_polymorphic_php_classes_virt<> : public virtual abstract_refcountable_php_interface {
public:
  refcountable_polymorphic_php_classes_virt() __attribute__((always_inline)) = default;

  void add_ref() noexcept final {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      ++refcnt_;
    }
  }

  uint32_t get_refcnt() noexcept final {
    return refcnt_;
  }

  void release() noexcept final __attribute__((always_inline)) {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      --refcnt_;
    }
    if (refcnt_ == 0) {
      delete this;
    }
  }

  void set_refcnt(uint32_t new_refcnt) noexcept final {
    refcnt_ = new_refcnt;
  }

  uint32_t &get_unique_index_ref() noexcept final {
    return unique_index_;
  }

private:
  uint32_t refcnt_{0};
  uint32_t unique_index_{0};
};

template<class Derived>
class refcountable_php_classes  : public ManagedThroughDlAllocator {
public:
  void add_ref() noexcept {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      ++refcnt_;
    }
  }

  uint32_t get_refcnt() noexcept {
    return refcnt_;
  }

  void release() noexcept __attribute__((always_inline)) {
    if (refcnt_ < ExtraRefCnt::for_global_const) {
      --refcnt_;
    }

    if (refcnt_ == 0) {
      static_assert(!std::is_polymorphic<Derived>{}, "Derived may not be polymorphic");
      /**
       * because of inheritance from ManagedThroughDlAllocator, which override operators new/delete
       * we should have vptr for passing proper sizeof of Derived class, but we don't want to increase size of every class
       * therefore we use static_cast here
       */
      delete static_cast<Derived *>(this);
    }
  }

  void set_refcnt(uint32_t new_refcnt) noexcept {
    refcnt_ = new_refcnt;
  }

  uint32_t &get_unique_index_ref() noexcept {
    return unique_index_;
  }

private:
  uint32_t refcnt_{0};
  uint32_t unique_index_{0};
};

class refcountable_empty_php_classes {
public:
  static void add_ref() noexcept {}
  static void release() noexcept {}
};
