#pragma once
#include <sys/types.h>

#include "common/mixin/not_copyable.h"
#include "common/cacheline.h"

class inter_process_mutex : vk::not_copyable {
public:
  void lock() noexcept;
  void unlock() noexcept;

private:
  alignas(KDB_CACHELINE_SIZE) pid_t lock_{0};
};
