#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xtypes.h>

#include "generated/default_mp_init.h"
#include "../../common/native_input.h"

REXCVAR_DEFINE_DOUBLE(native_mouse_sensitivity, 5.0, "Input",
                      "Native BO2 mouse sensitivity")
    .range(0.1, 30.0);

namespace {

rex::input::InputSystem* input_system;

constexpr uint32_t kCommandBuilder = 0x82206D28;
constexpr uint32_t kImpXamInputGetState = 0x827FB814;
constexpr uint32_t kMaxLocalClients = 0x82B1F754;
constexpr uint32_t kActiveClientPointer = 0x82B1F75C;
constexpr uint32_t kActiveClientStride = 0x8A3D0;
constexpr uint32_t kPitchOffset = 11404;
constexpr uint32_t kYawOffset = 11408;
constexpr uint32_t kCommandButtonsOffset = 4;
constexpr uint32_t kCommandForwardMoveOffset = 36;
constexpr uint32_t kCommandRightMoveOffset = 37;

constexpr uint32_t kButtonAttack = 0x1;
constexpr uint32_t kButtonSprint = 0x2;
constexpr uint32_t kButtonMelee = 0x4;
constexpr uint32_t kButtonUse = 0x8;
constexpr uint32_t kButtonReload = 0x10;
constexpr uint32_t kButtonCrouch = 0x200;
constexpr uint32_t kButtonJump = 0x400;
constexpr uint32_t kButtonAds = 0x800;

PPCFunc* original_command_builder;

#if defined(__ANDROID__)
uint32_t android_packet_number;

rex::X_RESULT GetAndroidInputState(rex::input::X_INPUT_STATE* state) {
  if (!state) {
    return 0;
  }

  bo2::AndroidControllerState pad{};
  {
    std::lock_guard lock(bo2::GetAndroidControllerMutex());
    pad = bo2::GetAndroidControllerState();
  }

  state->packet_number = ++android_packet_number;
  uint16_t buttons = 0;
  buttons |= pad.dpad_up ? rex::input::X_INPUT_GAMEPAD_DPAD_UP : 0;
  buttons |= pad.dpad_down ? rex::input::X_INPUT_GAMEPAD_DPAD_DOWN : 0;
  buttons |= pad.dpad_left ? rex::input::X_INPUT_GAMEPAD_DPAD_LEFT : 0;
  buttons |= pad.dpad_right ? rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT : 0;
  buttons |= pad.start ? rex::input::X_INPUT_GAMEPAD_START : 0;
  buttons |= pad.back ? rex::input::X_INPUT_GAMEPAD_BACK : 0;
  buttons |= pad.left_stick ? rex::input::X_INPUT_GAMEPAD_LEFT_THUMB : 0;
  buttons |= pad.right_stick ? rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB : 0;
  buttons |= pad.left_bumper ? rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER : 0;
  buttons |= pad.right_bumper ? rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER : 0;
  buttons |= pad.a ? rex::input::X_INPUT_GAMEPAD_A : 0;
  buttons |= pad.b ? rex::input::X_INPUT_GAMEPAD_B : 0;
  buttons |= pad.x ? rex::input::X_INPUT_GAMEPAD_X : 0;
  buttons |= pad.y ? rex::input::X_INPUT_GAMEPAD_Y : 0;
  state->gamepad.buttons = buttons;
  state->gamepad.left_trigger = static_cast<uint8_t>(std::clamp(pad.left_trigger, 0.0f, 1.0f) * 255.0f);
  state->gamepad.right_trigger = static_cast<uint8_t>(std::clamp(pad.right_trigger, 0.0f, 1.0f) * 255.0f);
  state->gamepad.thumb_lx = static_cast<int16_t>(std::clamp(pad.left_x, -1.0f, 1.0f) * 32767.0f);
  state->gamepad.thumb_ly = static_cast<int16_t>(std::clamp(-pad.left_y, -1.0f, 1.0f) * 32767.0f);
  state->gamepad.thumb_rx = static_cast<int16_t>(std::clamp(pad.right_x, -1.0f, 1.0f) * 32767.0f);
  state->gamepad.thumb_ry = static_cast<int16_t>(std::clamp(-pad.right_y, -1.0f, 1.0f) * 32767.0f);
  return 0;
}
#endif

float LoadFloat(uint32_t address, uint8_t* base) {
  return std::bit_cast<float>(REX_LOAD_U32(address));
}

void StoreFloat(uint32_t address, float value, uint8_t* base) {
  REX_STORE_U32(address, std::bit_cast<uint32_t>(value));
}

uint16_t UiCompatibilityKey(uint16_t key) {
  switch (key) {
    case 13:
    case 32:
      return 1;   // Confirm.
    case 27:
      return 2;   // Back.
    case 127:
      return 2;   // Back.
    case 154:
      return 20;
    case 155:
      return 21;
    case 156:
      return 22;
    case 157:
      return 23;
    default:
      return 0;
  }
}

void InstallHostDetour(PPCFunc* target, PPCFunc* replacement) {
#if REX_PLATFORM_WIN32
  constexpr size_t kPatchSize = 12;
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  DWORD old_protect = 0;
  if (!VirtualProtect(patch, kPatchSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
    REXLOG_ERROR("Failed to install BO2 native input host detour");
    return;
  }
  patch[0] = 0x48;
  patch[1] = 0xB8;
  *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(replacement);
  patch[10] = 0xFF;
  patch[11] = 0xE0;
  FlushInstructionCache(GetCurrentProcess(), patch, kPatchSize);
  DWORD unused_protect = 0;
  VirtualProtect(patch, kPatchSize, old_protect, &unused_protect);
#endif
}

PPCFunc* InstallHostDetourWithTrampoline(PPCFunc* target, PPCFunc* replacement) {
#if REX_PLATFORM_WIN32
  constexpr size_t kPatchSize = 12;
  constexpr size_t kTrampolineSize = kPatchSize + 12;
  constexpr uint8_t kExpectedPrologue[kPatchSize] = {
      0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xD6,
  };
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  if (std::memcmp(patch, kExpectedPrologue, kPatchSize) != 0) {
    REXLOG_ERROR("Unexpected BO2 command builder prologue; gameplay input hook disabled");
    return nullptr;
  }

  auto* trampoline = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE,
                   PAGE_EXECUTE_READWRITE));
  if (!trampoline) {
    REXLOG_ERROR("Failed to allocate BO2 command builder trampoline");
    return nullptr;
  }
  std::memcpy(trampoline, patch, kPatchSize);
  trampoline[kPatchSize] = 0x48;
  trampoline[kPatchSize + 1] = 0xB8;
  *reinterpret_cast<uint64_t*>(trampoline + kPatchSize + 2) =
      reinterpret_cast<uint64_t>(patch + kPatchSize);
  trampoline[kPatchSize + 10] = 0xFF;
  trampoline[kPatchSize + 11] = 0xE0;

  DWORD old_protect = 0;
  if (!VirtualProtect(patch, kPatchSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    REXLOG_ERROR("Failed to install BO2 command builder host detour");
    return nullptr;
  }
  patch[0] = 0x48;
  patch[1] = 0xB8;
  *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(replacement);
  patch[10] = 0xFF;
  patch[11] = 0xE0;
  FlushInstructionCache(GetCurrentProcess(), patch, kPatchSize);
  DWORD unused_protect = 0;
  VirtualProtect(patch, kPatchSize, old_protect, &unused_protect);
  return reinterpret_cast<PPCFunc*>(trampoline);
#else
  return nullptr;
#endif
}

REX_HOOK_RAW(default_mp_native_command_builder) {
  const uint32_t local_client = ctx.r3.u32;
  const uint32_t command = ctx.r4.u32;
  original_command_builder(ctx, base);

  if (command == 0 || local_client >= REX_LOAD_U32(kMaxLocalClients)) {
    return;
  }

  const auto input = bo2::NativeInput::Read();
  uint32_t buttons = REX_LOAD_U32(command + kCommandButtonsOffset);
  buttons |= input.attack ? kButtonAttack : 0;
  buttons |= input.sprint ? kButtonSprint : 0;
  buttons |= input.melee ? kButtonMelee : 0;
  buttons |= input.use ? kButtonUse : 0;
  buttons |= input.reload ? kButtonReload : 0;
  buttons |= input.crouch ? kButtonCrouch : 0;
  buttons |= input.jump ? kButtonJump : 0;
  buttons |= input.ads ? kButtonAds : 0;
  REX_STORE_U32(command + kCommandButtonsOffset, buttons);

  const int8_t forward_move =
      static_cast<int8_t>((input.forward ? 127 : 0) - (input.back ? 127 : 0));
  const int8_t right_move =
      static_cast<int8_t>((input.right ? 127 : 0) - (input.left ? 127 : 0));
  REX_STORE_U8(command + kCommandForwardMoveOffset,
               static_cast<uint8_t>(forward_move));
  REX_STORE_U8(command + kCommandRightMoveOffset,
               static_cast<uint8_t>(right_move));

  if (input.mouse_dx == 0 && input.mouse_dy == 0) {
    return;
  }
  const uint32_t active_clients = REX_LOAD_U32(kActiveClientPointer);
  if (!active_clients) {
    return;
  }
  const uint32_t active_client = active_clients + local_client * kActiveClientStride;
  const float scale = static_cast<float>(REXCVAR_GET(native_mouse_sensitivity) * 0.022);
  const float pitch =
      std::clamp(LoadFloat(active_client + kPitchOffset, base) + input.mouse_dy * scale,
                 -85.0f, 85.0f);
  const float yaw = LoadFloat(active_client + kYawOffset, base) - input.mouse_dx * scale;
  StoreFloat(active_client + kPitchOffset, pitch, base);
  StoreFloat(active_client + kYawOffset, yaw, base);
}

REX_HOOK_RAW(default_mp_native_input_tick) {
  constexpr uint32_t kXInputFlagGamepad = 0x01;
  constexpr uint32_t kXInputFlagAnyUser = 1u << 30;
  const uint32_t flags = ctx.r4.u32;
  if ((flags & 0xFF) && !(flags & kXInputFlagGamepad)) {
    ctx.r3.u64 = static_cast<rex::X_RESULT>(0x48F);
  } else {
    uint32_t user = ctx.r3.u32;
    if ((user & 0xFF) == 0xFF || (flags & kXInputFlagAnyUser)) {
      user = 0;
    }
    auto* state = ctx.r5.u32
                      ? reinterpret_cast<rex::input::X_INPUT_STATE*>(base + ctx.r5.u32)
                      : nullptr;
#if defined(__ANDROID__)
    ctx.r3.u64 = GetAndroidInputState(state);
#else
    ctx.r3.u64 = input_system->GetState(user, state);
#endif
  }
  const uint64_t result = ctx.r3.u64;

  bo2::NativeKeyEvent event{};
  while (bo2::NativeInput::PopKeyEvent(event)) {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = 0;
    frame.ctx.r4.u64 = event.key;
    frame.ctx.r5.u64 = event.down ? 1 : 0;
    frame.ctx.r6.u64 = event.time;
    sub_82216D00(frame.ctx, base);

    if (const uint16_t ui_key = UiCompatibilityKey(event.key)) {
      rex::CallFrame ui_frame(ctx);
      ui_frame.ctx.r3.u64 = 0;
      ui_frame.ctx.r4.u64 = ui_key;
      ui_frame.ctx.r5.u64 = event.down ? 1 : 0;
      ui_frame.ctx.r6.u64 = event.time;
      sub_82216D00(ui_frame.ctx, base);
    }
  }
  ctx.r3.u64 = result;
}

}  // namespace

namespace bo2 {

void InstallDefaultMpNativeInput(rex::Runtime* runtime) {
  input_system = static_cast<rex::input::InputSystem*>(runtime->input_system());
  original_command_builder = InstallHostDetourWithTrampoline(
      &sub_82206D28, &default_mp_native_command_builder);
  if (!original_command_builder) {
#if defined(__ANDROID__)
    original_command_builder = &sub_82206D28;
#else
    return;
#endif
  }
  runtime->function_dispatcher()->SetFunction(kCommandBuilder,
                                               &default_mp_native_command_builder);
  runtime->function_dispatcher()->SetFunction(kImpXamInputGetState,
                                               &default_mp_native_input_tick);
  InstallHostDetour(&__imp__XamInputGetState, &default_mp_native_input_tick);
  REXLOG_INFO("BO2 native keyboard/mouse gameplay hooks installed at {:#010x} and {:#010x}",
              kCommandBuilder, kImpXamInputGetState);
}

}  // namespace bo2
