#pragma once

#include <zeno/zen.h>
#include <zeno/vec.h>

namespace zen {


struct ConditionObject : IObjectClone<ConditionObject> {
  bool value = true;

  bool get() {
    return value;
  }

  void set(bool x) {
    value = x;
  }
};

}
