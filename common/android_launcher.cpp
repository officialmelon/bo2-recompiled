#include "android_launcher.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <map>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>

#include "native_input.h"

namespace rex::ui {
void SetAndroidNativeWindow(ANativeWindow* window, uint32_t width, uint32_t height);
void PumpAndroidWindowFrame();
}

namespace {
class AndroidWindowedAppContext final : public rex::ui::WindowedAppContext {
 public:
  void BindCurrentThreadAsUIThread() { ui_thread_id_ = std::this_thread::get_id(); }

 protected:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

std::mutex g_app_mutex;
std::unique_ptr<AndroidWindowedAppContext> g_app_context;
std::unique_ptr<rex::ui::WindowedApp> g_app;
std::atomic_bool g_pump_running{false};
std::thread g_pump_thread;

std::string AppIdentifierForLibraryName(const char* app) {
  if (std::strcmp(app, "default") == 0) {
    return "default_app";
  }
  return app;
}

void ConfigureAndroidPaths(const char* app, const char* mode, const char* game_data_root) {
  char app_name[] = "bo2-android";
  char* argv[] = {app_name};
  rex::cvar::Init(1, argv);
  rex::cvar::ApplyEnvironment();

  const std::string root = game_data_root && game_data_root[0]
                               ? game_data_root
                               : "/sdcard/BO2Recompiled";
  rex::cvar::SetFlagByName("game_data_root", root);
  rex::cvar::SetFlagByName("user_data_root", root + "/user/" + app);
  rex::cvar::SetFlagByName("cache_path", root + "/cache/" + app);
  rex::cvar::SetFlagByName("vulkan_require_fill_mode_non_solid", "false");
  if (mode && mode[0]) {
    rex::cvar::SetFlagByName("mode", mode);
  }
}

void StopPumpThread() {
  g_pump_running.store(false, std::memory_order_release);
  if (g_pump_thread.joinable()) {
    g_pump_thread.join();
  }
}

void StartPumpThread() {
  g_pump_running.store(true, std::memory_order_release);
  g_pump_thread = std::thread([] {
    {
      std::lock_guard lock(g_app_mutex);
      if (g_app_context) {
        g_app_context->BindCurrentThreadAsUIThread();
      }
    }
    while (g_pump_running.load(std::memory_order_acquire)) {
      {
        std::lock_guard lock(g_app_mutex);
        if (g_app_context) {
          g_app_context->ExecutePendingFunctionsFromUIThread();
        }
        rex::ui::PumpAndroidWindowFrame();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  });
}

rex::ui::VirtualKey VirtualKeyFromAndroidKeyCode(int key_code) {
  switch (key_code) {
    case 19:  // KEYCODE_DPAD_UP
      return rex::ui::VirtualKey::kUp;
    case 20:  // KEYCODE_DPAD_DOWN
      return rex::ui::VirtualKey::kDown;
    case 21:  // KEYCODE_DPAD_LEFT
      return rex::ui::VirtualKey::kLeft;
    case 22:  // KEYCODE_DPAD_RIGHT
      return rex::ui::VirtualKey::kRight;
    case 23:   // KEYCODE_DPAD_CENTER
    case 66:   // KEYCODE_ENTER
    case 96:   // KEYCODE_BUTTON_A
    case 108:  // KEYCODE_BUTTON_START
      return rex::ui::VirtualKey::kReturn;
    case 4:    // KEYCODE_BACK
    case 97:   // KEYCODE_BUTTON_B
    case 109:  // KEYCODE_BUTTON_SELECT
      return rex::ui::VirtualKey::kEscape;
    case 99:  // KEYCODE_BUTTON_X
      return rex::ui::VirtualKey::kX;
    case 100:  // KEYCODE_BUTTON_Y
      return rex::ui::VirtualKey::kY;
    case 102:  // KEYCODE_BUTTON_L1
      return rex::ui::VirtualKey::kQ;
    case 103:  // KEYCODE_BUTTON_R1
      return rex::ui::VirtualKey::kE;
    default:
      return rex::ui::VirtualKey::kNone;
  }
}
}  // namespace

extern "C" {
// These would be called by the Java side to set up the JVM reference
static JavaVM* g_jvm = nullptr;
static jobject g_native_bridge_obj = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// Example JNI method to register the bridge object
JNIEXPORT void JNICALL Java_com_rex_bo2_NativeBridge_registerBridge(JNIEnv* env, jobject obj) {
    if (g_native_bridge_obj) {
        env->DeleteGlobalRef(g_native_bridge_obj);
    }
    g_native_bridge_obj = env->NewGlobalRef(obj);
}

JNIEXPORT jboolean JNICALL Java_com_rex_bo2_MainActivity_nativeStartApp(
    JNIEnv* env, jobject thiz, jstring app_name, jstring mode_name, jstring game_data_root,
    jobject surface, jint width, jint height) {
    (void)thiz;
    const char* app = env->GetStringUTFChars(app_name, nullptr);
    const char* mode = env->GetStringUTFChars(mode_name, nullptr);
    const char* root = env->GetStringUTFChars(game_data_root, nullptr);

    __android_log_print(ANDROID_LOG_INFO, "BO2",
                        "nativeStartApp begin app=%s mode=%s root=%s size=%dx%d",
                        app ? app : "", mode ? mode : "", root ? root : "",
                        static_cast<int>(width), static_cast<int>(height));

    ANativeWindow* native_window = ANativeWindow_fromSurface(env, surface);
    rex::ui::SetAndroidNativeWindow(native_window, static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height));
    if (native_window) {
        ANativeWindow_release(native_window);
    }

    bool ok = false;
    StopPumpThread();
    {
        std::lock_guard lock(g_app_mutex);
        if (g_app_context) {
            g_app_context->BindCurrentThreadAsUIThread();
        }
        if (g_app) {
            g_app->InvokeOnDestroy();
            g_app.reset();
        }
        g_app_context.reset();
        g_app_context = std::make_unique<AndroidWindowedAppContext>();
        ConfigureAndroidPaths(app, mode, root);

        const std::string identifier = AppIdentifierForLibraryName(app);
        auto creator = rex::ui::WindowedApp::GetCreator(identifier);
        if (!creator) {
            __android_log_print(ANDROID_LOG_ERROR, "BO2", "No app creator for %s", identifier.c_str());
        } else {
            g_app = creator(*g_app_context);
            g_app->SetParsedArguments({});
            __android_log_print(ANDROID_LOG_INFO, "BO2", "OnInitialize begin for %s",
                                identifier.c_str());
            ok = g_app->OnInitialize();
            __android_log_print(ANDROID_LOG_INFO, "BO2", "OnInitialize returned %s for %s",
                                ok ? "true" : "false", identifier.c_str());
            if (ok) {
                g_app_context->ExecutePendingFunctionsFromUIThread();
                StartPumpThread();
            } else {
                g_app->InvokeOnDestroy();
                g_app.reset();
                g_app_context.reset();
                rex::ui::SetAndroidNativeWindow(nullptr, 0, 0);
            }
        }
    }

    env->ReleaseStringUTFChars(app_name, app);
    env->ReleaseStringUTFChars(mode_name, mode);
    env->ReleaseStringUTFChars(game_data_root, root);
    __android_log_print(ANDROID_LOG_INFO, "BO2", "nativeStartApp end ok=%s",
                        ok ? "true" : "false");
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_rex_bo2_MainActivity_nativeStopApp(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    StopPumpThread();
    std::lock_guard lock(g_app_mutex);
    if (g_app_context) {
        g_app_context->BindCurrentThreadAsUIThread();
    }
    if (g_app) {
        g_app->InvokeOnDestroy();
        g_app.reset();
    }
    g_app_context.reset();
    rex::ui::SetAndroidNativeWindow(nullptr, 0, 0);
}

JNIEXPORT void JNICALL Java_com_rex_bo2_MainActivity_nativeFrame(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
}

JNIEXPORT void JNICALL Java_com_rex_bo2_MainActivity_nativeToggleDebugOverlay(
    JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    std::lock_guard lock(g_app_mutex);
    if (!g_app_context) {
        return;
    }
    g_app_context->CallInUIThreadDeferred([] {
        rex::ui::KeyEvent event(nullptr, rex::ui::VirtualKey::kF3, 1, false,
                                false, false, false, false);
        rex::ui::ProcessKeyEvent(event);
    });
}

JNIEXPORT void JNICALL Java_com_rex_bo2_MainActivity_nativeSendKeyEvent(
    JNIEnv* env, jobject thiz, jint key_code, jboolean down) {
    (void)env;
    (void)thiz;
    const auto virtual_key = VirtualKeyFromAndroidKeyCode(static_cast<int>(key_code));
    if (virtual_key == rex::ui::VirtualKey::kNone) {
        return;
    }

    std::lock_guard lock(g_app_mutex);
    if (!g_app_context) {
        return;
    }
    g_app_context->CallInUIThreadDeferred([virtual_key, is_down = down == JNI_TRUE] {
        bo2::NativeInput::InjectVirtualKey(virtual_key, is_down);
        rex::ui::KeyEvent event(nullptr, virtual_key, 1, !is_down,
                                false, false, false, false);
        rex::ui::ProcessKeyEvent(event);
    });
}

JNIEXPORT void JNICALL Java_com_rex_bo2_MainActivity_nativeSetControllerState(
    JNIEnv* env, jobject thiz, jfloat left_x, jfloat left_y, jfloat right_x, jfloat right_y,
    jfloat left_trigger, jfloat right_trigger, jboolean a, jboolean b, jboolean x, jboolean y,
    jboolean left_bumper, jboolean right_bumper, jboolean left_stick, jboolean right_stick,
    jboolean dpad_up, jboolean dpad_down, jboolean dpad_left, jboolean dpad_right,
    jboolean start, jboolean back) {
    (void)env;
    (void)thiz;
    bo2::AndroidControllerState state{};
    state.left_x = left_x;
    state.left_y = left_y;
    state.right_x = right_x;
    state.right_y = right_y;
    state.left_trigger = left_trigger;
    state.right_trigger = right_trigger;
    state.a = a;
    state.b = b;
    state.x = x;
    state.y = y;
    state.left_bumper = left_bumper;
    state.right_bumper = right_bumper;
    state.left_stick = left_stick;
    state.right_stick = right_stick;
    state.dpad_up = dpad_up;
    state.dpad_down = dpad_down;
    state.dpad_left = dpad_left;
    state.dpad_right = dpad_right;
    state.start = start;
    state.back = back;
    bo2::SetAndroidControllerState(state);
}

bool rex_android_get_xinput_state(rex::input::X_INPUT_STATE* state) {
    if (!state) {
        return false;
    }

    static std::atomic<uint32_t> packet_number{0};
    bo2::AndroidControllerState pad{};
    {
        std::lock_guard lock(bo2::GetAndroidControllerMutex());
        pad = bo2::GetAndroidControllerState();
    }

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

    state->packet_number = packet_number.fetch_add(1, std::memory_order_relaxed) + 1;
    state->gamepad.buttons = buttons;
    state->gamepad.left_trigger = static_cast<uint8_t>(std::clamp(pad.left_trigger, 0.0f, 1.0f) * 255.0f);
    state->gamepad.right_trigger = static_cast<uint8_t>(std::clamp(pad.right_trigger, 0.0f, 1.0f) * 255.0f);
    state->gamepad.thumb_lx = static_cast<int16_t>(std::clamp(pad.left_x, -1.0f, 1.0f) * 32767.0f);
    state->gamepad.thumb_ly = static_cast<int16_t>(std::clamp(-pad.left_y, -1.0f, 1.0f) * 32767.0f);
    state->gamepad.thumb_rx = static_cast<int16_t>(std::clamp(pad.right_x, -1.0f, 1.0f) * 32767.0f);
    state->gamepad.thumb_ry = static_cast<int16_t>(std::clamp(-pad.right_y, -1.0f, 1.0f) * 32767.0f);

    static std::atomic<uint16_t> previous_buttons{0};
    const uint16_t old_buttons = previous_buttons.exchange(buttons, std::memory_order_relaxed);
    if (buttons != old_buttons) {
        REXLOG_INFO("Android controller buttons {:04X} -> {:04X}", old_buttons, buttons);
    }

    static std::atomic<uint32_t> log_count{0};
    if ((buttons || pad.left_x || pad.left_y || pad.right_x || pad.right_y ||
         pad.left_trigger || pad.right_trigger) &&
        log_count.fetch_add(1, std::memory_order_relaxed) < 32) {
        REXLOG_INFO("Android controller state buttons={:04X} lx={} ly={} rx={} ry={} lt={} rt={}",
                    buttons, pad.left_x, pad.left_y, pad.right_x, pad.right_y,
                    pad.left_trigger, pad.right_trigger);
    }
    return true;
}
}

namespace bo2 {

void RequestAndroidRestart(const std::string& app, const std::string& mode) {
    if (!g_jvm || !g_native_bridge_obj) {
        __android_log_print(ANDROID_LOG_ERROR, "BO2", "JVM or NativeBridge not registered!");
        return;
    }

    JNIEnv* env = nullptr;
    jint get_env_result = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (!env) {
        g_jvm->AttachCurrentThread(&env, nullptr);
    }

    jclass clazz = env->GetObjectClass(g_native_bridge_obj);
    jmethodID method = env->GetMethodID(clazz, "requestRestart", "(Ljava/lang/String;Ljava/lang/String;)V");
    __android_log_print(ANDROID_LOG_INFO, "BO2",
                        "RequestAndroidRestart app=%s mode=%s getEnv=%d env=%p clazz=%p method=%p",
                        app.c_str(), mode.c_str(), get_env_result, env, clazz, method);
    if (!method || env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, "BO2", "RequestAndroidRestart failed before Java call");
        env->ExceptionDescribe();
        env->ExceptionClear();
        return;
    }
    
    jstring japp = env->NewStringUTF(app.c_str());
    jstring jmode = env->NewStringUTF(mode.c_str());
    
    env->CallVoidMethod(g_native_bridge_obj, method, japp, jmode);
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, "BO2", "RequestAndroidRestart Java call threw");
        env->ExceptionDescribe();
        env->ExceptionClear();
    } else {
        __android_log_print(ANDROID_LOG_INFO, "BO2", "RequestAndroidRestart Java call returned");
    }
    
    env->DeleteLocalRef(japp);
    env->DeleteLocalRef(jmode);
}

} // namespace bo2

#else

namespace bo2 {
void RequestAndroidRestart(const std::string& app, const std::string& mode) {
    // No-op on other platforms
}
} // namespace bo2

#endif
