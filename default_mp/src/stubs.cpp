#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include "generated/default_mp_init.h"
 
#define GUEST_FUNCTION_STUB(func_name) \
extern "C" PPC_FUNC(func_name) { \
    PPC_FUNC_PROLOGUE(); \
    ctx.r3.u64 = 0; \
}
 
#define GUEST_FUNCTION_STUB_FAIL(func_name) \
extern "C" PPC_FUNC(func_name) { \
    PPC_FUNC_PROLOGUE(); \
    ctx.r3.u64 = 0x80004005; \
}

GUEST_FUNCTION_STUB(__imp__XUsbcamCreate);
GUEST_FUNCTION_STUB(__imp__XUsbcamGetState);
GUEST_FUNCTION_STUB_FAIL(__imp__XUsbcamDestroy);
GUEST_FUNCTION_STUB_FAIL(__imp__XUsbcamSetConfig);
GUEST_FUNCTION_STUB_FAIL(__imp__XUsbcamSetCaptureMode);
GUEST_FUNCTION_STUB_FAIL(__imp__XUsbcamReadFrame);