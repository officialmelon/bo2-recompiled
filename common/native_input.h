#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace bo2 {

struct NativeInputSnapshot {
  int32_t mouse_dx;
  int32_t mouse_dy;
  bool forward;
  bool back;
  bool left;
  bool right;
  bool attack;
  bool ads;
  bool jump;
  bool crouch;
  bool sprint;
  bool reload;
  bool use;
  bool melee;
};

struct NativeKeyEvent {
  uint16_t key;
  bool down;
  uint32_t time;
};

#if defined(__ANDROID__)
struct AndroidControllerState {
  float left_x;
  float left_y;
  float right_x;
  float right_y;
  float left_trigger;
  float right_trigger;
  bool a;
  bool b;
  bool x;
  bool y;
  bool left_bumper;
  bool right_bumper;
  bool left_stick;
  bool right_stick;
  bool dpad_up;
  bool dpad_down;
  bool dpad_left;
  bool dpad_right;
  bool start;
  bool back;
};

inline AndroidControllerState& GetAndroidControllerState() {
  static AndroidControllerState state{};
  return state;
}

inline std::mutex& GetAndroidControllerMutex() {
  static std::mutex mutex;
  return mutex;
}

inline void SetAndroidControllerState(const AndroidControllerState& state) {
  std::lock_guard lock(GetAndroidControllerMutex());
  GetAndroidControllerState() = state;
}
#endif

class NativeInput final : public rex::ui::WindowListener,
                          public rex::ui::WindowInputListener {
 public:
  NativeInput() = default;
  ~NativeInput() override { Detach(); }

  void Attach(rex::ui::Window* window) {
    if (window_ == window) {
      return;
    }
    Detach();
    window_ = window;
    active_.store(this, std::memory_order_release);
    window_->AddListener(this);
    window_->AddInputListener(this, 100);
    SetCaptured(false);
  }

  void Detach() {
    if (!window_) {
      return;
    }
    SetCaptured(false);
    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
    window_ = nullptr;
    NativeInput* expected = this;
    active_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
  }

  static NativeInputSnapshot Read() {
    auto* input = active_.load(std::memory_order_acquire);
    if (!input || !input->captured_.load(std::memory_order_relaxed)) {
      return {};
    }

    NativeInputSnapshot result{};
    result.mouse_dx = input->mouse_dx_.exchange(0, std::memory_order_acq_rel);
    result.mouse_dy = input->mouse_dy_.exchange(0, std::memory_order_acq_rel);
    result.forward = input->Key(rex::ui::VirtualKey::kW);
    result.back = input->Key(rex::ui::VirtualKey::kS);
    result.left = input->Key(rex::ui::VirtualKey::kA);
    result.right = input->Key(rex::ui::VirtualKey::kD);
    result.attack = input->mouse_buttons_[0].load(std::memory_order_relaxed);
    result.ads = input->mouse_buttons_[1].load(std::memory_order_relaxed);
    result.jump = input->Key(rex::ui::VirtualKey::kSpace);
    result.crouch = input->Key(rex::ui::VirtualKey::kC) ||
                    input->Key(rex::ui::VirtualKey::kControl) ||
                    input->Key(rex::ui::VirtualKey::kLControl) ||
                    input->Key(rex::ui::VirtualKey::kRControl);
    result.sprint = input->Key(rex::ui::VirtualKey::kShift) ||
                    input->Key(rex::ui::VirtualKey::kLShift) ||
                    input->Key(rex::ui::VirtualKey::kRShift);
    result.reload = input->Key(rex::ui::VirtualKey::kR);
    result.use = input->Key(rex::ui::VirtualKey::kE);
    result.melee = input->Key(rex::ui::VirtualKey::kF) ||
                   input->Key(rex::ui::VirtualKey::kV);
#if defined(__ANDROID__)
    {
      std::lock_guard lock(GetAndroidControllerMutex());
      const auto& pad = GetAndroidControllerState();
      constexpr float kDeadzone = 0.22f;
      result.left = result.left || pad.left_x < -kDeadzone || pad.dpad_left;
      result.right = result.right || pad.left_x > kDeadzone || pad.dpad_right;
      result.forward = result.forward || pad.left_y < -kDeadzone || pad.dpad_up;
      result.back = result.back || pad.left_y > kDeadzone || pad.dpad_down;
      result.mouse_dx += static_cast<int32_t>(pad.right_x * 18.0f);
      result.mouse_dy += static_cast<int32_t>(pad.right_y * 18.0f);
      result.attack = result.attack || pad.right_trigger > 0.25f;
      result.ads = result.ads || pad.left_trigger > 0.25f;
      result.jump = result.jump || pad.a;
      result.crouch = result.crouch || pad.b || pad.right_stick;
      result.reload = result.reload || pad.x;
      result.use = result.use || pad.x;
      result.melee = result.melee || pad.right_bumper;
      result.sprint = result.sprint || pad.left_stick;
    }
#endif
    return result;
  }

  static bool PopKeyEvent(NativeKeyEvent& event) {
    auto* input = active_.load(std::memory_order_acquire);
    if (!input || input->pending_events_.load(std::memory_order_acquire) == 0) {
      return false;
    }
    std::lock_guard lock(input->events_mutex_);
    if (input->events_.empty()) {
      input->pending_events_.store(0, std::memory_order_release);
      return false;
    }
    event = input->events_.front();
    input->events_.pop_front();
    input->pending_events_.fetch_sub(1, std::memory_order_release);
    return true;
  }

  static void InjectVirtualKey(rex::ui::VirtualKey key, bool down) {
    auto* input = active_.load(std::memory_order_acquire);
    if (!input) {
      return;
    }
    input->SetKey(key, down);
    input->QueueKey(MapVirtualKey(key), down);
  }

  void OnLostFocus(rex::ui::UISetupEvent&) override {
    SetCaptured(false);
  }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    if (event.virtual_key() == rex::ui::VirtualKey::kF8 && !event.prev_state()) {
      SetCaptured(!captured_.load(std::memory_order_relaxed));
      event.set_handled(true);
      return;
    }
    SetKey(event.virtual_key(), true);
    QueueKey(MapVirtualKey(event.virtual_key()), true);
  }

  void OnKeyUp(rex::ui::KeyEvent& event) override {
    SetKey(event.virtual_key(), false);
    QueueKey(MapVirtualKey(event.virtual_key()), false);
  }

  void OnMouseDown(rex::ui::MouseEvent& event) override {
    if (!captured_.load(std::memory_order_relaxed)) {
      SetCaptured(true);
    }
    SetMouseButton(event.button(), true);
    QueueKey(MapMouseButton(event.button()), true);
  }

  void OnMouseUp(rex::ui::MouseEvent& event) override {
    SetMouseButton(event.button(), false);
    QueueKey(MapMouseButton(event.button()), false);
  }

  void OnMouseWheel(rex::ui::MouseEvent& event) override {
    const uint16_t key = event.scroll_y() > 0 ? 206 : event.scroll_y() < 0 ? 205 : 0;
    if (key) {
      QueueKey(key, true);
      QueueKey(key, false);
    }
  }

  void OnMouseMove(rex::ui::MouseEvent& event) override {
    if (!window_ || !captured_.load(std::memory_order_relaxed)) {
      return;
    }
    const int32_t center_x = static_cast<int32_t>(window_->GetActualLogicalWidth() / 2);
    const int32_t center_y = static_cast<int32_t>(window_->GetActualLogicalHeight() / 2);
    const int32_t dx = event.x() - center_x;
    const int32_t dy = event.y() - center_y;
    if (dx == 0 && dy == 0) {
      return;
    }
    mouse_dx_.fetch_add(dx, std::memory_order_relaxed);
    mouse_dy_.fetch_add(dy, std::memory_order_relaxed);
    CenterCursor();
    event.set_handled(true);
  }

 private:
  static uint16_t MapVirtualKey(rex::ui::VirtualKey key) {
    const uint16_t value = static_cast<uint16_t>(key);
    if (value >= static_cast<uint16_t>(rex::ui::VirtualKey::kA) &&
        value <= static_cast<uint16_t>(rex::ui::VirtualKey::kZ)) {
      return value + ('a' - 'A');
    }
    if ((value >= '0' && value <= '9') || value == ' ' || value == '\t' ||
        value == '\r' || value == 27) {
      return value;
    }

    switch (key) {
      case rex::ui::VirtualKey::kBack:
        return 127;
      case rex::ui::VirtualKey::kUp:
        return 154;
      case rex::ui::VirtualKey::kDown:
        return 155;
      case rex::ui::VirtualKey::kLeft:
        return 156;
      case rex::ui::VirtualKey::kRight:
        return 157;
      case rex::ui::VirtualKey::kMenu:
      case rex::ui::VirtualKey::kLMenu:
      case rex::ui::VirtualKey::kRMenu:
        return 158;
      case rex::ui::VirtualKey::kControl:
      case rex::ui::VirtualKey::kLControl:
      case rex::ui::VirtualKey::kRControl:
        return 159;
      case rex::ui::VirtualKey::kShift:
      case rex::ui::VirtualKey::kLShift:
      case rex::ui::VirtualKey::kRShift:
        return 160;
      case rex::ui::VirtualKey::kInsert:
        return 161;
      case rex::ui::VirtualKey::kDelete:
        return 162;
      case rex::ui::VirtualKey::kNext:
        return 163;
      case rex::ui::VirtualKey::kPrior:
        return 164;
      case rex::ui::VirtualKey::kHome:
        return 165;
      case rex::ui::VirtualKey::kEnd:
        return 166;
      case rex::ui::VirtualKey::kF1:
      case rex::ui::VirtualKey::kF2:
      case rex::ui::VirtualKey::kF3:
      case rex::ui::VirtualKey::kF4:
      case rex::ui::VirtualKey::kF5:
      case rex::ui::VirtualKey::kF6:
      case rex::ui::VirtualKey::kF7:
      case rex::ui::VirtualKey::kF8:
      case rex::ui::VirtualKey::kF9:
      case rex::ui::VirtualKey::kF10:
      case rex::ui::VirtualKey::kF11:
      case rex::ui::VirtualKey::kF12:
        return 167 + value - static_cast<uint16_t>(rex::ui::VirtualKey::kF1);
      case rex::ui::VirtualKey::kOem1:
        return ';';
      case rex::ui::VirtualKey::kOemPlus:
        return '=';
      case rex::ui::VirtualKey::kOemComma:
        return ',';
      case rex::ui::VirtualKey::kOemMinus:
        return '-';
      case rex::ui::VirtualKey::kOemPeriod:
        return '.';
      case rex::ui::VirtualKey::kOem2:
        return '/';
      case rex::ui::VirtualKey::kOem3:
        return '`';
      case rex::ui::VirtualKey::kOem4:
        return '[';
      case rex::ui::VirtualKey::kOem5:
        return '\\';
      case rex::ui::VirtualKey::kOem6:
        return ']';
      case rex::ui::VirtualKey::kOem7:
        return '\'';
      default:
        return 0;
    }
  }

  static uint16_t MapMouseButton(rex::ui::MouseEvent::Button button) {
    switch (button) {
      case rex::ui::MouseEvent::Button::kLeft:
        return 200;
      case rex::ui::MouseEvent::Button::kRight:
        return 201;
      case rex::ui::MouseEvent::Button::kMiddle:
        return 202;
      case rex::ui::MouseEvent::Button::kX1:
        return 203;
      case rex::ui::MouseEvent::Button::kX2:
        return 204;
      default:
        return 0;
    }
  }

  void QueueKey(uint16_t key, bool down) {
    if (!key || !captured_.load(std::memory_order_relaxed)) {
      return;
    }
#if REX_PLATFORM_WIN32
    const uint32_t time = GetTickCount();
#else
    const uint32_t time = 0;
#endif
    std::lock_guard lock(events_mutex_);
    if (events_.size() >= 256) {
      events_.pop_front();
      pending_events_.fetch_sub(1, std::memory_order_relaxed);
    }
    events_.push_back({key, down, time});
    pending_events_.fetch_add(1, std::memory_order_release);
  }

  bool Key(rex::ui::VirtualKey key) const {
    const auto index = static_cast<uint16_t>(key);
    return index < keys_.size() && keys_[index].load(std::memory_order_relaxed);
  }

  void SetKey(rex::ui::VirtualKey key, bool down) {
    const auto index = static_cast<uint16_t>(key);
    if (index < keys_.size()) {
      keys_[index].store(down, std::memory_order_relaxed);
    }
  }

  void SetMouseButton(rex::ui::MouseEvent::Button button, bool down) {
    size_t index;
    switch (button) {
      case rex::ui::MouseEvent::Button::kLeft:
        index = 0;
        break;
      case rex::ui::MouseEvent::Button::kRight:
        index = 1;
        break;
      default:
        return;
    }
    mouse_buttons_[index].store(down, std::memory_order_relaxed);
  }

  void SetCaptured(bool captured) {
    if (!window_ || captured_.exchange(captured, std::memory_order_acq_rel) == captured) {
      return;
    }
    mouse_dx_.store(0, std::memory_order_relaxed);
    mouse_dy_.store(0, std::memory_order_relaxed);
    if (captured) {
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      window_->CaptureMouse();
      CenterCursor();
    } else {
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      window_->ReleaseMouse();
      for (auto& key : keys_) {
        key.store(false, std::memory_order_relaxed);
      }
      for (auto& button : mouse_buttons_) {
        button.store(false, std::memory_order_relaxed);
      }
      std::lock_guard lock(events_mutex_);
      events_.clear();
      pending_events_.store(0, std::memory_order_release);
    }
  }

  void CenterCursor() {
#if REX_PLATFORM_WIN32
    if (!window_) {
      return;
    }
    auto hwnd = static_cast<HWND>(window_->GetNativeWindowHandle());
    if (!hwnd) {
      return;
    }
    POINT point{
        static_cast<LONG>(window_->GetActualLogicalWidth() / 2),
        static_cast<LONG>(window_->GetActualLogicalHeight() / 2),
    };
    ClientToScreen(hwnd, &point);
    SetCursorPos(point.x, point.y);
#endif
  }

  inline static std::atomic<NativeInput*> active_{nullptr};
  rex::ui::Window* window_ = nullptr;
  std::array<std::atomic_bool, 256> keys_{};
  std::array<std::atomic_bool, 2> mouse_buttons_{};
  std::mutex events_mutex_;
  std::deque<NativeKeyEvent> events_;
  std::atomic<uint32_t> pending_events_{0};
  std::atomic_bool captured_{false};
  std::atomic<int32_t> mouse_dx_{0};
  std::atomic<int32_t> mouse_dy_{0};
};

}  // namespace bo2
