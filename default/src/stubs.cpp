#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include "generated/default_init.h"
#include <atomic>
#include <rex/logging.h>

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

// large amount of stubs!!

GUEST_FUNCTION_STUB(_XNetLogonGetTitleID);
GUEST_FUNCTION_STUB(_XamUserGetOnlineCountryFromXUID);
GUEST_FUNCTION_STUB(_XamUserGetMembershipTierFromXUID);
GUEST_FUNCTION_STUB(_XamGetLanguage);
GUEST_FUNCTION_STUB_FAIL(_XamAvatarManifestGetBodyType);
GUEST_FUNCTION_STUB_FAIL(_XamAvatarLoadAnimation);
GUEST_FUNCTION_STUB_FAIL(_XamAvatarGetManifestLocalUser);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetUnregisterInAddr);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetRegisterKey);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetQosLookup);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetGetConnectStatus);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetConnect);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpStartup);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpShutdown);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpCrackUrl);
GUEST_FUNCTION_STUB_FAIL(_NetDll_WSAGetOverlappedResult);
GUEST_FUNCTION_STUB(_LDIResetDecompression);
GUEST_FUNCTION_STUB(_LDIDestroyDecompression);
GUEST_FUNCTION_STUB_FAIL(_LDIDecompress);
GUEST_FUNCTION_STUB_FAIL(_LDICreateDecompression);
GUEST_FUNCTION_STUB(_XamShowMarketplaceDownloadItemsUI);
GUEST_FUNCTION_STUB(_XamShowGamerCardUIForXUID);
GUEST_FUNCTION_STUB(_XamShowFriendsUI);
GUEST_FUNCTION_STUB_FAIL(_XamQueryLiveHiveA);
GUEST_FUNCTION_STUB(_XampXAuthStartup);
GUEST_FUNCTION_STUB(_XampXAuthShutdown);
GUEST_FUNCTION_STUB_FAIL(_XampXAuthGetTitleBuffer);
GUEST_FUNCTION_STUB_FAIL(_XamGetToken);
GUEST_FUNCTION_STUB(_XamFreeToken);
GUEST_FUNCTION_STUB(_XamBackgroundDownloadSetMode);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpSetStatusCallback);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpSendRequest);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpReceiveResponse);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpReadData);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpQueryHeaders);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpOpenRequest);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpOpen);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpDoWork);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpConnect);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XHttpCloseHandle);
GUEST_FUNCTION_STUB(_XAudioGetDuckerThreshold);
GUEST_FUNCTION_STUB(_XAudioGetDuckerReleaseTime);
GUEST_FUNCTION_STUB(_XAudioGetDuckerLevel);
GUEST_FUNCTION_STUB(_XAudioGetDuckerHoldTime);
GUEST_FUNCTION_STUB(_XAudioGetDuckerAttackTime);
GUEST_FUNCTION_STUB(_XamVoiceSubmitPacket);
GUEST_FUNCTION_STUB_FAIL(_XamUserCreateStatsEnumerator);
GUEST_FUNCTION_STUB_FAIL(_XamContentCreateEnumerator);
GUEST_FUNCTION_STUB_FAIL(_XamContentAggregateCreateEnumerator);
GUEST_FUNCTION_STUB_FAIL(_XamContentCreateDeviceEnumerator);
GUEST_FUNCTION_STUB(_XamShowMarketplaceUI);
GUEST_FUNCTION_STUB(_XamShowFriendRequestUI);
GUEST_FUNCTION_STUB(_XamShowAchievementsUI);
GUEST_FUNCTION_STUB(_XamContentInstall);
GUEST_FUNCTION_STUB_FAIL(_NetDll_XNetReplaceKey);
