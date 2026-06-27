#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#include "generated/default_init.h"

namespace bo2 {

namespace {

constexpr uint32_t kImpXNetGetTitleXnAddr = 0x826EB70C;
constexpr uint32_t kImpXNetGetEthernetLinkStatus = 0x826EB71C;

constexpr uint32_t kXNetGetXnAddrEthernet = 0x00000002;
constexpr uint32_t kXNetGetXnAddrStatic = 0x00000004;

constexpr uint32_t kXNetEthernetLinkActive = 0x01;
constexpr uint32_t kXNetEthernetLink100Mbps = 0x02;
constexpr uint32_t kXNetEthernetLinkFullDuplex = 0x08;

struct XNADDR {
  rex::be<uint32_t> ina;
  rex::be<uint32_t> ina_online;
  rex::be<uint16_t> port_online;
  uint8_t ab_enet[6];
  uint8_t ab_online[20];
};

uint32_t NetDllXNetGetTitleXnAddrOverride(uint32_t caller, ppc_ptr_t<XNADDR> addr_ptr) {
  (void)caller;
  if (addr_ptr) {
    addr_ptr->ina = 0x7F000001;
    addr_ptr->ina_online = 0x7F000001;
    addr_ptr->port_online = 3074;
    std::memset(addr_ptr->ab_enet, 0xCC, sizeof(addr_ptr->ab_enet));
    std::memset(addr_ptr->ab_online, 0, sizeof(addr_ptr->ab_online));
  }
  return kXNetGetXnAddrEthernet | kXNetGetXnAddrStatic | 0x00000080;
}

uint32_t NetDllXNetGetEthernetLinkStatusOverride(uint32_t caller) {
  (void)caller;
#if defined(__ANDROID__)
  return 0;
#else
  return kXNetEthernetLinkActive | kXNetEthernetLink100Mbps | kXNetEthernetLinkFullDuplex;
#endif
}

REX_HOOK(default__NetDllXNetGetTitleXnAddr, NetDllXNetGetTitleXnAddrOverride)
REX_HOOK(default__NetDllXNetGetEthernetLinkStatus, NetDllXNetGetEthernetLinkStatusOverride)

}  // namespace

void InstallDefaultXamOverrides(rex::Runtime* runtime) {
  auto* dispatcher = runtime->function_dispatcher();
  dispatcher->SetFunction(kImpXNetGetTitleXnAddr, &default__NetDllXNetGetTitleXnAddr);
  dispatcher->SetFunction(kImpXNetGetEthernetLinkStatus, &default__NetDllXNetGetEthernetLinkStatus);
}

}  // namespace bo2
