#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <rex/runtime.h>
#include <rex/kernel/xboxkrnl/error.h>
#include <rex/ppc/function.h>
#include <rex/string.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xio.h>
#include <rex/system/xsocket.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>

#if REX_PLATFORM_WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#endif

namespace default_mp {

void InstallXamOverrides(rex::Runtime* runtime);

}  // namespace default_mp

namespace {

using namespace rex;
using namespace rex::system;

constexpr uint32_t kAppXgi = 0xFB;
constexpr uint32_t kAppXLiveBase = 0xFC;

constexpr uint32_t kMsgXgiSessionCreate = 0x000B0010;
constexpr uint32_t kMsgXgiSessionDelete = 0x000B0011;
constexpr uint32_t kMsgXgiSessionJoinLocal = 0x000B0012;
constexpr uint32_t kMsgXgiSessionLeaveLocal = 0x000B0013;
constexpr uint32_t kMsgXLiveGetServiceInfo = 0x00058007;

constexpr uint32_t kImpXMsgStartIORequestEx = 0x827FB674;
constexpr uint32_t kImpXMsgStartIORequest = 0x827FB684;
constexpr uint32_t kImpXMsgInProcessCall = 0x827FB694;
constexpr uint32_t kImpXamUserCheckPrivilege = 0x827FB714;
constexpr uint32_t kImpXMsgCancelIORequest = 0x827FB844;
constexpr uint32_t kImpXamGetOverlappedResult = 0x827FB8C4;
constexpr uint32_t kImpXMsgCompleteIORequest = 0x827FB8D4;

constexpr uint32_t kImpXNetXnAddrToInAddr = 0x827FC354;
constexpr uint32_t kImpXNetInAddrToXnAddr = 0x827FC374;
constexpr uint32_t kImpXNetInAddrToString = 0x827FC384;
constexpr uint32_t kImpXNetXnAddrToMachineId = 0x827FC3A4;
constexpr uint32_t kImpXNetQosListen = 0x827FC3F4;
constexpr uint32_t kImpXNetQosServiceLookup = 0x827FC414;
constexpr uint32_t kImpXNetGetTitleXnAddr = 0x827FC434;
constexpr uint32_t kImpXNetGetEthernetLinkStatus = 0x827FC444;
constexpr uint32_t kImpNetRecvFrom = 0x827FC504;
constexpr uint32_t kImpNetSendTo = 0x827FC524;

constexpr uint32_t kWsaeWouldBlock = 0x2733;
constexpr uint32_t kWsaeNotSock = 0x2736;

struct XMSGSTARTIOREQUEST_UNKNOWNARG {
  be<uint32_t> unk_0;
  be<uint32_t> unk_1;
};

struct XNADDR {
  be<uint32_t> ina;
  be<uint32_t> ina_online;
  be<uint16_t> port_online;
  uint8_t ab_enet[6];
  uint8_t ab_online[20];
};

struct XNQOSINFO {
  uint8_t flags;
  uint8_t reserved;
  be<uint16_t> probes_xmit;
  be<uint16_t> probes_recv;
  be<uint16_t> data_len;
  be<uint32_t> data_ptr;
  be<uint16_t> rtt_min_in_msecs;
  be<uint16_t> rtt_med_in_msecs;
  be<uint32_t> up_bits_per_sec;
  be<uint32_t> down_bits_per_sec;
};

struct XNQOS {
  be<uint32_t> count;
  be<uint32_t> count_pending;
  XNQOSINFO info[1];
};

constexpr uint32_t kXNetGetXnAddrEthernet = 0x00000002;
constexpr uint32_t kXNetGetXnAddrStatic = 0x00000004;

constexpr uint32_t kXNetEthernetLinkActive = 0x01;
constexpr uint32_t kXNetEthernetLink100Mbps = 0x02;
constexpr uint32_t kXNetEthernetLinkFullDuplex = 0x08;

X_HRESULT DispatchSync(uint32_t app, uint32_t message, uint32_t buffer_ptr, uint32_t buffer_length) {
  return REX_KERNEL_STATE()->app_manager()->DispatchMessageSync(app, message, buffer_ptr, buffer_length);
}

X_HRESULT DispatchAsync(uint32_t app, uint32_t message, uint32_t buffer_ptr,
                        uint32_t buffer_length) {
  return REX_KERNEL_STATE()->app_manager()->DispatchMessageAsync(app, message, buffer_ptr, buffer_length);
}

bool HandleXgiMessage(uint32_t message, uint32_t buffer_ptr, uint32_t buffer_length,
                      X_HRESULT& out_result) {
  auto* memory = REX_KERNEL_MEMORY();
  auto* buffer = buffer_ptr ? memory->TranslateVirtual(buffer_ptr) : nullptr;

  switch (message) {
    case kMsgXgiSessionCreate: {
      if (buffer_length && buffer_length != 28) {
        out_result = X_E_INVALIDARG;
        return true;
      }
      if (!buffer) {
        out_result = X_E_SUCCESS;
        return true;
      }

      uint32_t session_info_ptr = memory::load_and_swap<uint32_t>(buffer + 0x14);
      uint32_t nonce_ptr = memory::load_and_swap<uint32_t>(buffer + 0x18);

      if (session_info_ptr) {
        auto* info = memory->TranslateVirtual(session_info_ptr);
        memory::store_and_swap<uint32_t>(info + 0x00, 0xAB1234CD);
        memory::store_and_swap<uint32_t>(info + 0x04, 0xEF5678AB);
        memory::store_and_swap<uint32_t>(info + 0x08, 0xC0A80164);
        memory::store_and_swap<uint32_t>(info + 0x0C, 0);
        memory::store_and_swap<uint16_t>(info + 0x10, 0);
        std::memset(info + 0x12, 0xCC, 6);
        std::memset(info + 0x18, 0, 20);
        std::memset(info + 0x2C, 0xAB, 16);
      }

      if (nonce_ptr) {
        auto* nonce = memory->TranslateVirtual(nonce_ptr);
        memory::store_and_swap<uint32_t>(nonce + 0, 0x12345678);
        memory::store_and_swap<uint32_t>(nonce + 4, 0x9ABCDEF0);
      }

      out_result = X_E_SUCCESS;
      return true;
    }
    case kMsgXgiSessionDelete:
    case kMsgXgiSessionJoinLocal:
    case kMsgXgiSessionLeaveLocal:
      out_result = X_E_SUCCESS;
      return true;
    default:
      return false;
  }
}

bool HandleXLiveBaseMessage(uint32_t message, X_HRESULT& out_result) {
  if (message != kMsgXLiveGetServiceInfo) {
    return false;
  }
  out_result = X_E_SUCCESS;
  return true;
}

X_HRESULT DispatchWithOverrides(uint32_t app, uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) {
  X_HRESULT result = X_ERROR_NOT_FOUND;
  if (app == kAppXgi && HandleXgiMessage(message, buffer_ptr, buffer_length, result)) {
    return result;
  }
  if (app == kAppXLiveBase && HandleXLiveBaseMessage(message, result)) {
    return result;
  }
  return DispatchSync(app, message, buffer_ptr, buffer_length);
}

X_HRESULT StartIoRequestWithOverrides(uint32_t app, uint32_t message, uint32_t overlapped_ptr,
                                      uint32_t buffer_ptr, uint32_t buffer_length) {
  X_HRESULT result = X_ERROR_NOT_FOUND;
  bool handled = false;
  if (app == kAppXgi) {
    handled = HandleXgiMessage(message, buffer_ptr, buffer_length, result);
  } else if (app == kAppXLiveBase) {
    handled = HandleXLiveBaseMessage(message, result);
  }

  if (!handled) {
    result = DispatchAsync(app, message, buffer_ptr, buffer_length);
    if (result == X_E_NOTFOUND) {
      result = X_E_INVALIDARG;
      XThread::SetLastError(X_ERROR_NOT_FOUND);
    }
  }

  if (overlapped_ptr) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }

  if (result == X_ERROR_SUCCESS || result == X_ERROR_IO_PENDING) {
    XThread::SetLastError(0);
  }
  return result;
}

ppc_u32_result_t XamUserCheckPrivilegeOverride(ppc_u32_t user_index, ppc_u32_t mask,
                                               ppc_pu32_t out_value) {
  (void)mask;
  if (user_index != 0xFF) {
    if (user_index >= 4) {
      return X_ERROR_INVALID_PARAMETER;
    }
    if (user_index) {
      return X_ERROR_NO_SUCH_USER;
    }
  }

  if (out_value) {
    *out_value = 1;
  }
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XMsgInProcessCallOverride(ppc_u32_t app, ppc_u32_t message, ppc_u32_t arg1,
                                           ppc_u32_t arg2) {
  return DispatchWithOverrides(app, message, arg1, arg2);
}

ppc_u32_result_t XMsgStartIORequestExOverride(ppc_u32_t app, ppc_u32_t message,
                                              ppc_ptr_t<XAM_OVERLAPPED> overlapped_ptr,
                                              ppc_u32_t buffer_ptr, ppc_u32_t buffer_length,
                                              ppc_ptr_t<XMSGSTARTIOREQUEST_UNKNOWNARG> unknown_ptr) {
  (void)unknown_ptr;
  return StartIoRequestWithOverrides(app, message, overlapped_ptr.guest_address(), buffer_ptr,
                                     buffer_length);
}

ppc_u32_result_t XMsgStartIORequestOverride(ppc_u32_t app, ppc_u32_t message,
                                            ppc_ptr_t<XAM_OVERLAPPED> overlapped_ptr,
                                            ppc_u32_t buffer_ptr, ppc_u32_t buffer_length) {
  return StartIoRequestWithOverrides(app, message, overlapped_ptr.guest_address(), buffer_ptr,
                                     buffer_length);
}

ppc_u32_result_t XMsgCancelIORequestOverride(ppc_ptr_t<XAM_OVERLAPPED> overlapped_ptr,
                                             ppc_u32_t wait) {
  X_HANDLE event_handle = XOverlappedGetEvent(overlapped_ptr);
  if (event_handle && wait) {
    auto ev = REX_KERNEL_OBJECTS()->LookupObject<XEvent>(event_handle);
    if (ev) {
      ev->Wait(0, 0, true, nullptr);
    }
  }
  return 0;
}

ppc_u32_result_t XMsgCompleteIORequestOverride(ppc_ptr_t<XAM_OVERLAPPED> overlapped_ptr,
                                               ppc_u32_t result, ppc_u32_t extended_error,
                                               ppc_u32_t length) {
  REX_KERNEL_STATE()->CompleteOverlappedImmediateEx(overlapped_ptr.guest_address(), result,
                                                    extended_error, length);
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XamGetOverlappedResultOverride(ppc_ptr_t<XAM_OVERLAPPED> overlapped_ptr,
                                                ppc_pu32_t length_ptr, ppc_u32_t unknown) {
  (void)unknown;
  uint32_t result = 0;
  if (overlapped_ptr->result != X_ERROR_IO_PENDING) {
    result = overlapped_ptr->result;
  } else if (!overlapped_ptr->event) {
    result = X_ERROR_IO_INCOMPLETE;
  } else {
    auto ev = REX_KERNEL_OBJECTS()->LookupObject<XEvent>(overlapped_ptr->event);
    result = ev->Wait(3, 1, 0, nullptr);
    if (XSUCCEEDED(result)) {
      result = overlapped_ptr->result;
    } else {
      result = kernel::xboxkrnl::xeRtlNtStatusToDosError(result);
    }
  }
  if (XSUCCEEDED(result) && length_ptr) {
    *length_ptr = overlapped_ptr->length;
  }
  return result;
}

ppc_u32_result_t NetDllXNetGetTitleXnAddrOverride(ppc_u32_t caller, ppc_ptr_t<XNADDR> addr_ptr) {
  (void)caller;
  addr_ptr->ina = 0xC0A80164;
  addr_ptr->ina_online = 0;
  addr_ptr->port_online = 0;
  std::memset(addr_ptr->ab_enet, 0xCC, sizeof(addr_ptr->ab_enet));
  std::memset(addr_ptr->ab_online, 0, sizeof(addr_ptr->ab_online));
  return kXNetGetXnAddrEthernet | kXNetGetXnAddrStatic;
}

ppc_u32_result_t NetDllXNetXnAddrToMachineIdOverride(ppc_u32_t caller, ppc_ptr_t<XNADDR> addr_ptr,
                                                     ppc_pu32_t id_ptr) {
  (void)caller;
  (void)addr_ptr;
  if (id_ptr) {
    *id_ptr = 1;
  }
  return 0;
}

void NetDllXNetInAddrToStringOverride(ppc_u32_t caller, ppc_u32_t in_addr_val,
                                      ppc_pchar_t string_out, ppc_u32_t string_size) {
  (void)caller;
  (void)in_addr_val;
  rex::string::rex_strcpy(string_out, string_size, "127.0.0.1");
}

ppc_u32_result_t NetDllXNetXnAddrToInAddrOverride(ppc_u32_t caller, ppc_ptr_t<XNADDR> xn_addr,
                                                  ppc_pvoid_t xid, ppc_pvoid_t in_addr) {
  (void)caller;
  (void)xid;
  if (in_addr && xn_addr) {
    auto* out = REX_KERNEL_MEMORY()->TranslateVirtual(in_addr.guest_address());
    memory::store<uint32_t>(out, xn_addr->ina);
  }
  return 0;
}

ppc_u32_result_t NetDllXNetInAddrToXnAddrOverride(ppc_u32_t caller, ppc_pvoid_t in_addr,
                                                  ppc_ptr_t<XNADDR> xn_addr, ppc_pvoid_t xid) {
  (void)caller;
  (void)xid;
  if (xn_addr && in_addr) {
    auto* src = REX_KERNEL_MEMORY()->TranslateVirtual(in_addr.guest_address());
    xn_addr->ina = memory::load<uint32_t>(src);
    xn_addr->ina_online = 0;
    xn_addr->port_online = 0;
    std::memset(xn_addr->ab_enet, 0xCC, sizeof(xn_addr->ab_enet));
    std::memset(xn_addr->ab_online, 0, sizeof(xn_addr->ab_online));
  }
  return 0;
}

ppc_u32_result_t NetDllXNetQosServiceLookupOverride(ppc_u32_t caller, ppc_u32_t flags,
                                                    ppc_u32_t event_handle, ppc_pu32_t pqos) {
  (void)caller;
  (void)flags;
  if (pqos) {
    uint32_t qos_guest = REX_KERNEL_MEMORY()->SystemHeapAlloc(sizeof(XNQOS));
    auto* qos = REX_KERNEL_MEMORY()->TranslateVirtual<XNQOS*>(qos_guest);
    qos->count = 1;
    qos->count_pending = 0;
    std::memset(&qos->info[0], 0, sizeof(qos->info[0]));
    *pqos = qos_guest;
  }
  if (event_handle) {
    auto ev = REX_KERNEL_OBJECTS()->LookupObject<XEvent>(event_handle);
    if (ev) {
      ev->Set(0, false);
    }
  }
  return 0;
}

ppc_u32_result_t NetDllXNetQosListenOverride(ppc_u32_t caller, ppc_pvoid_t id, ppc_pvoid_t data,
                                             ppc_u32_t data_size, ppc_u32_t r7, ppc_u32_t flags) {
  (void)caller;
  (void)id;
  (void)data;
  (void)data_size;
  (void)r7;
  (void)flags;
  return 0;
}

ppc_u32_result_t NetDllXNetGetEthernetLinkStatusOverride(ppc_u32_t caller) {
  (void)caller;
  return kXNetEthernetLinkActive | kXNetEthernetLink100Mbps | kXNetEthernetLinkFullDuplex;
}

ppc_u32_result_t NetDllRecvFromOverride(ppc_u32_t caller, ppc_u32_t socket_handle,
                                        ppc_pvoid_t buf_ptr, ppc_u32_t buf_len, ppc_u32_t flags,
                                        ppc_ptr_t<XSOCKADDR_IN> from_ptr,
                                        ppc_pu32_t fromlen_ptr) {
  (void)caller;
  auto socket = REX_KERNEL_OBJECTS()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(kWsaeNotSock);
    return static_cast<uint32_t>(-1);
  }

  N_XSOCKADDR_IN native_from;
  if (from_ptr) {
    native_from = *from_ptr;
  }
  uint32_t native_fromlen = fromlen_ptr ? fromlen_ptr.value() : 0;
  int ret = socket->RecvFrom(buf_ptr, buf_len, flags, &native_from, fromlen_ptr ? &native_fromlen : nullptr);

  if (from_ptr) {
    from_ptr->sin_family = native_from.sin_family;
    from_ptr->sin_port = native_from.sin_port;
    from_ptr->sin_addr = native_from.sin_addr;
    std::memset(from_ptr->x_sin_zero, 0, sizeof(from_ptr->x_sin_zero));
  }
  if (fromlen_ptr) {
    *fromlen_ptr = native_fromlen;
  }

  if (ret == -1) {
#if REX_PLATFORM_WIN32
    uint32_t error_code = WSAGetLastError();
    XThread::SetLastError(error_code ? error_code : kWsaeWouldBlock);
#else
    XThread::SetLastError(kWsaeWouldBlock);
#endif
    return static_cast<uint32_t>(-1);
  }

  XThread::SetLastError(0);
  return static_cast<uint32_t>(ret);
}

ppc_u32_result_t NetDllSendToOverride(ppc_u32_t caller, ppc_u32_t socket_handle,
                                      ppc_pvoid_t buf_ptr, ppc_u32_t buf_len, ppc_u32_t flags,
                                      ppc_ptr_t<XSOCKADDR_IN> to_ptr, ppc_u32_t to_len) {
  (void)caller;
  auto socket = REX_KERNEL_OBJECTS()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(kWsaeNotSock);
    return static_cast<uint32_t>(-1);
  }

  N_XSOCKADDR_IN native_to(to_ptr);
  int ret = socket->SendTo(buf_ptr, buf_len, flags, &native_to, to_len);
  if (ret == -1) {
#if REX_PLATFORM_WIN32
    XThread::SetLastError(WSAGetLastError());
#else
    XThread::SetLastError(0);
#endif
    return static_cast<uint32_t>(-1);
  }

  XThread::SetLastError(0);
  return static_cast<uint32_t>(ret);
}

void InstallHostDetour(const char* name, PPCFunc* replacement) {
#if REX_PLATFORM_WIN32
  auto* target = rex::FindPPCFuncByName(name);
  assert_not_null(target);

  constexpr size_t kPatchSize = 12;
  DWORD old_protect = 0;
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  BOOL protect_ok =
      VirtualProtect(patch, kPatchSize, PAGE_EXECUTE_READWRITE, &old_protect);
  assert_true(protect_ok);

  patch[0] = 0x48;
  patch[1] = 0xB8;
  *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(replacement);
  patch[10] = 0xFF;
  patch[11] = 0xE0;

  FlushInstructionCache(GetCurrentProcess(), patch, kPatchSize);

  DWORD unused_protect = 0;
  VirtualProtect(patch, kPatchSize, old_protect, &unused_protect);
#else
  (void)name;
  (void)replacement;
  assert_always();
#endif
}

PPC_HOOK(default_mp__XamUserCheckPrivilege, XamUserCheckPrivilegeOverride)
PPC_HOOK(default_mp__XMsgInProcessCall, XMsgInProcessCallOverride)
PPC_HOOK(default_mp__XMsgStartIORequestEx, XMsgStartIORequestExOverride)
PPC_HOOK(default_mp__XMsgStartIORequest, XMsgStartIORequestOverride)
PPC_HOOK(default_mp__XMsgCancelIORequest, XMsgCancelIORequestOverride)
PPC_HOOK(default_mp__XMsgCompleteIORequest, XMsgCompleteIORequestOverride)
PPC_HOOK(default_mp__XamGetOverlappedResult, XamGetOverlappedResultOverride)
PPC_HOOK(default_mp__NetDllXNetGetTitleXnAddr, NetDllXNetGetTitleXnAddrOverride)
PPC_HOOK(default_mp__NetDllXNetXnAddrToMachineId, NetDllXNetXnAddrToMachineIdOverride)
PPC_HOOK(default_mp__NetDllXNetInAddrToString, NetDllXNetInAddrToStringOverride)
PPC_HOOK(default_mp__NetDllXNetXnAddrToInAddr, NetDllXNetXnAddrToInAddrOverride)
PPC_HOOK(default_mp__NetDllXNetInAddrToXnAddr, NetDllXNetInAddrToXnAddrOverride)
PPC_HOOK(default_mp__NetDllXNetQosServiceLookup, NetDllXNetQosServiceLookupOverride)
PPC_HOOK(default_mp__NetDllXNetQosListen, NetDllXNetQosListenOverride)
PPC_HOOK(default_mp__NetDllXNetGetEthernetLinkStatus, NetDllXNetGetEthernetLinkStatusOverride)
PPC_HOOK(default_mp__NetDllRecvFrom, NetDllRecvFromOverride)
PPC_HOOK(default_mp__NetDllSendTo, NetDllSendToOverride)

}  // namespace

void default_mp::InstallXamOverrides(rex::Runtime* runtime) {
  auto* dispatcher = runtime->function_dispatcher();
  dispatcher->SetFunction(kImpXamUserCheckPrivilege, &default_mp__XamUserCheckPrivilege);
  dispatcher->SetFunction(kImpXMsgInProcessCall, &default_mp__XMsgInProcessCall);
  dispatcher->SetFunction(kImpXMsgStartIORequestEx, &default_mp__XMsgStartIORequestEx);
  dispatcher->SetFunction(kImpXMsgStartIORequest, &default_mp__XMsgStartIORequest);
  dispatcher->SetFunction(kImpXMsgCancelIORequest, &default_mp__XMsgCancelIORequest);
  dispatcher->SetFunction(kImpXMsgCompleteIORequest, &default_mp__XMsgCompleteIORequest);
  dispatcher->SetFunction(kImpXamGetOverlappedResult, &default_mp__XamGetOverlappedResult);

  dispatcher->SetFunction(kImpXNetGetTitleXnAddr, &default_mp__NetDllXNetGetTitleXnAddr);
  dispatcher->SetFunction(kImpXNetXnAddrToMachineId, &default_mp__NetDllXNetXnAddrToMachineId);
  dispatcher->SetFunction(kImpXNetInAddrToString, &default_mp__NetDllXNetInAddrToString);
  dispatcher->SetFunction(kImpXNetXnAddrToInAddr, &default_mp__NetDllXNetXnAddrToInAddr);
  dispatcher->SetFunction(kImpXNetInAddrToXnAddr, &default_mp__NetDllXNetInAddrToXnAddr);
  dispatcher->SetFunction(kImpXNetQosServiceLookup, &default_mp__NetDllXNetQosServiceLookup);
  dispatcher->SetFunction(kImpXNetQosListen, &default_mp__NetDllXNetQosListen);
  dispatcher->SetFunction(kImpXNetGetEthernetLinkStatus, &default_mp__NetDllXNetGetEthernetLinkStatus);
  dispatcher->SetFunction(kImpNetRecvFrom, &default_mp__NetDllRecvFrom);
  dispatcher->SetFunction(kImpNetSendTo, &default_mp__NetDllSendTo);

  InstallHostDetour("__imp__XamUserCheckPrivilege", &default_mp__XamUserCheckPrivilege);
  InstallHostDetour("__imp__XMsgInProcessCall", &default_mp__XMsgInProcessCall);
  InstallHostDetour("__imp__XMsgStartIORequestEx", &default_mp__XMsgStartIORequestEx);
  InstallHostDetour("__imp__XMsgStartIORequest", &default_mp__XMsgStartIORequest);
  InstallHostDetour("__imp__XMsgCancelIORequest", &default_mp__XMsgCancelIORequest);
  InstallHostDetour("__imp__XMsgCompleteIORequest", &default_mp__XMsgCompleteIORequest);
  InstallHostDetour("__imp__XamGetOverlappedResult", &default_mp__XamGetOverlappedResult);

  InstallHostDetour("__imp__NetDll_XNetGetTitleXnAddr", &default_mp__NetDllXNetGetTitleXnAddr);
  InstallHostDetour("__imp__NetDll_XNetXnAddrToMachineId",
                    &default_mp__NetDllXNetXnAddrToMachineId);
  InstallHostDetour("__imp__NetDll_XNetInAddrToString", &default_mp__NetDllXNetInAddrToString);
  InstallHostDetour("__imp__NetDll_XNetXnAddrToInAddr", &default_mp__NetDllXNetXnAddrToInAddr);
  InstallHostDetour("__imp__NetDll_XNetInAddrToXnAddr", &default_mp__NetDllXNetInAddrToXnAddr);
  InstallHostDetour("__imp__NetDll_XNetQosServiceLookup",
                    &default_mp__NetDllXNetQosServiceLookup);
  InstallHostDetour("__imp__NetDll_XNetQosListen", &default_mp__NetDllXNetQosListen);
  InstallHostDetour("__imp__NetDll_XNetGetEthernetLinkStatus",
                    &default_mp__NetDllXNetGetEthernetLinkStatus);
  InstallHostDetour("__imp__NetDll_recvfrom", &default_mp__NetDllRecvFrom);
  InstallHostDetour("__imp__NetDll_sendto", &default_mp__NetDllSendTo);
}
