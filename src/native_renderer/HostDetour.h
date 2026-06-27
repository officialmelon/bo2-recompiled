#pragma once

#include <rex/ppc/context.h>

namespace bo2::native {

bool InstallHostDetour(PPCFunc* target, PPCFunc* replacement, const char* name);

}  // namespace bo2::native
