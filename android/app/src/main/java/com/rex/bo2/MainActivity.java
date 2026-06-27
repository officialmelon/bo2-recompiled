package com.rex.bo2;

import android.Manifest;
import android.app.Activity;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "BO2";
    private static final int REQUEST_STORAGE = 100;

    private NativeBridge mBridge;
    private SurfaceView mSurfaceView;
    private String mApp;
    private String mMode;
    private String mGameDataRoot;
    private volatile boolean mStarted;
    private volatile boolean mStarting;
    private boolean mNativeLoaded;
    private AudioFocusRequest mAudioFocusRequest;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }

        mApp = getIntent().getStringExtra("app");
        if (mApp == null || mApp.isEmpty()) {
            mApp = "default_mp";
        }

        mMode = getIntent().getStringExtra("mode");
        if (mMode == null || mMode.isEmpty()) {
            mMode = "zombies";
        }

        String root = getIntent().getStringExtra("game_data_root");
        if (root == null || root.isEmpty()) {
            root = new File(Environment.getExternalStorageDirectory(), "BO2Recompiled").getAbsolutePath();
        }
        mGameDataRoot = root;

        Log.i(TAG, "Starting app=" + mApp + " mode=" + mMode + " root=" + mGameDataRoot);

        if (!ensureStorageAccess()) {
            Log.w(TAG, "Waiting for all-files storage permission before native startup");
            return;
        }
        initializeNativeUi();
        hideSystemUi();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        startNative(holder);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        startNative(holder);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mNativeLoaded) {
            nativeStopApp();
        }
        mStarted = false;
    }

    @Override
    protected void onDestroy() {
        if (mNativeLoaded) {
            nativeStopApp();
        }
        abandonAudioFocus();
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mSurfaceView == null && ensureStorageAccess()) {
            initializeNativeUi();
        }
        hideSystemUi();
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (isGamepadEvent(event)) {
            updateControllerAxes(event);
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (isGamepadSource(event.getSource()) || isControllerKeyCode(event.getKeyCode())) {
            Log.i(TAG, "Gamepad key code=" + event.getKeyCode()
                    + " action=" + event.getAction() + " device=" + event.getDeviceId());
            if (event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_MODE) {
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                    nativeToggleDebugOverlay();
                }
                return true;
            }
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                nativeSendKeyEvent(event.getKeyCode(), true);
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                nativeSendKeyEvent(event.getKeyCode(), false);
            }
            updateControllerButton(event.getKeyCode(), event.getAction() != KeyEvent.ACTION_UP);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    public void restartWith(String app, String mode) {
        Log.i(TAG, "Restarting with app=" + app + " mode=" + mode + " root=" + mGameDataRoot);
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("app", app);
        intent.putExtra("mode", mode);
        intent.putExtra("game_data_root", mGameDataRoot);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        int pendingFlags = PendingIntent.FLAG_CANCEL_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) {
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent restartIntent = PendingIntent.getActivity(this, 1, intent, pendingFlags);
        AlarmManager alarmManager = (AlarmManager) getSystemService(Context.ALARM_SERVICE);
        alarmManager.set(AlarmManager.ELAPSED_REALTIME_WAKEUP,
                SystemClock.elapsedRealtime() + 500, restartIntent);
        Log.i(TAG, "Scheduled replacement activity; exiting current process");
        finishAffinity();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    private void startNative(SurfaceHolder holder) {
        if (!mNativeLoaded) {
            return;
        }
        if (mStarted || mStarting) {
            return;
        }
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            return;
        }
        int width = Math.max(1, mSurfaceView.getWidth());
        int height = Math.max(1, mSurfaceView.getHeight());
        mStarting = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                boolean started = nativeStartApp(mApp, mMode, mGameDataRoot, surface, width, height);
                Log.i(TAG, "nativeStartApp returned " + started);
                mStarted = started;
                mStarting = false;
                if (!started) {
                    Log.e(TAG, "Native app failed to start. Put game files in " + mGameDataRoot);
                }
            }
        }, "BO2NativeStart").start();
    }

    private void initializeNativeUi() {
        requestAudioFocus();
        if (!mNativeLoaded) {
            System.loadLibrary(mApp);
            mNativeLoaded = true;
        }

        mBridge = new NativeBridge(this);
        mBridge.register();

        mSurfaceView = new SurfaceView(this);
        mSurfaceView.getHolder().addCallback(this);
        mSurfaceView.setFocusable(true);
        mSurfaceView.setFocusableInTouchMode(true);
        mSurfaceView.requestFocus();
        mSurfaceView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        setContentView(mSurfaceView);
    }

    private void requestAudioFocus() {
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        AudioManager audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (audioManager == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 26) {
            AudioAttributes attributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build();
            mAudioFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(attributes)
                    .setOnAudioFocusChangeListener(new AudioManager.OnAudioFocusChangeListener() {
                        @Override
                        public void onAudioFocusChange(int focusChange) {
                        }
                    })
                    .build();
            audioManager.requestAudioFocus(mAudioFocusRequest);
        } else {
            audioManager.requestAudioFocus(null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        }
    }

    private void abandonAudioFocus() {
        AudioManager audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (audioManager == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 26 && mAudioFocusRequest != null) {
            audioManager.abandonAudioFocusRequest(mAudioFocusRequest);
        } else {
            audioManager.abandonAudioFocus(null);
        }
    }

    private void hideSystemUi() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    private boolean ensureStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivity(intent);
                } catch (Exception ignored) {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                }
                return false;
            }
            return true;
        }
        if (Build.VERSION.SDK_INT < 23) {
            return true;
        }
        if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, REQUEST_STORAGE);
            return false;
        }
        return true;
    }

    private boolean isGamepadEvent(MotionEvent event) {
        return (event.getAction() == MotionEvent.ACTION_MOVE) && isGamepadSource(event.getSource());
    }

    private boolean isGamepadSource(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private float axis(MotionEvent event, int axis) {
        float value = event.getAxisValue(axis);
        return Math.abs(value) < 0.12f ? 0.0f : value;
    }

    private void updateControllerAxes(MotionEvent event) {
        mLeftX = axis(event, MotionEvent.AXIS_X);
        mLeftY = axis(event, MotionEvent.AXIS_Y);
        mRightX = axis(event, MotionEvent.AXIS_Z);
        mRightY = axis(event, MotionEvent.AXIS_RZ);
        mLeftTrigger = Math.max(axis(event, MotionEvent.AXIS_LTRIGGER), axis(event, MotionEvent.AXIS_BRAKE));
        mRightTrigger = Math.max(axis(event, MotionEvent.AXIS_RTRIGGER), axis(event, MotionEvent.AXIS_GAS));
        float hatX = axis(event, MotionEvent.AXIS_HAT_X);
        float hatY = axis(event, MotionEvent.AXIS_HAT_Y);
        mDpadLeft = hatX < -0.5f;
        mDpadRight = hatX > 0.5f;
        mDpadUp = hatY < -0.5f;
        mDpadDown = hatY > 0.5f;
        flushControllerState();
    }

    private void updateControllerButton(int keyCode, boolean down) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: mA = down; break;
            case KeyEvent.KEYCODE_BUTTON_B: mB = down; break;
            case KeyEvent.KEYCODE_BUTTON_X: mX = down; break;
            case KeyEvent.KEYCODE_BUTTON_Y: mY = down; break;
            case KeyEvent.KEYCODE_BUTTON_L1: mLeftBumper = down; break;
            case KeyEvent.KEYCODE_BUTTON_R1: mRightBumper = down; break;
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_MENU: mStart = down; break;
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BACK: mBack = down; break;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: mLeftStick = down; break;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: mRightStick = down; break;
            case KeyEvent.KEYCODE_DPAD_UP: mDpadUp = down; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: mDpadDown = down; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: mDpadLeft = down; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: mDpadRight = down; break;
            default: return;
        }
        flushControllerState();
    }

    private static boolean isControllerKeyCode(int keyCode) {
        return (keyCode >= KeyEvent.KEYCODE_BUTTON_A && keyCode <= KeyEvent.KEYCODE_BUTTON_MODE)
                || keyCode == KeyEvent.KEYCODE_DPAD_UP
                || keyCode == KeyEvent.KEYCODE_DPAD_DOWN
                || keyCode == KeyEvent.KEYCODE_DPAD_LEFT
                || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT;
    }

    private void flushControllerState() {
        nativeSetControllerState(
                mLeftX, mLeftY, mRightX, mRightY, mLeftTrigger, mRightTrigger,
                mA, mB, mX, mY, mLeftBumper, mRightBumper, mLeftStick, mRightStick,
                mDpadUp, mDpadDown, mDpadLeft, mDpadRight, mStart, mBack);
    }

    private float mLeftX;
    private float mLeftY;
    private float mRightX;
    private float mRightY;
    private float mLeftTrigger;
    private float mRightTrigger;
    private boolean mA;
    private boolean mB;
    private boolean mX;
    private boolean mY;
    private boolean mLeftBumper;
    private boolean mRightBumper;
    private boolean mLeftStick;
    private boolean mRightStick;
    private boolean mDpadUp;
    private boolean mDpadDown;
    private boolean mDpadLeft;
    private boolean mDpadRight;
    private boolean mStart;
    private boolean mBack;

    private native boolean nativeStartApp(String app, String mode, String gameDataRoot,
                                          Surface surface, int width, int height);
    private native void nativeStopApp();
    private native void nativeToggleDebugOverlay();
    private native void nativeSendKeyEvent(int keyCode, boolean down);
    private native void nativeSetControllerState(
            float leftX, float leftY, float rightX, float rightY,
            float leftTrigger, float rightTrigger,
            boolean a, boolean b, boolean x, boolean y,
            boolean leftBumper, boolean rightBumper,
            boolean leftStick, boolean rightStick,
            boolean dpadUp, boolean dpadDown, boolean dpadLeft, boolean dpadRight,
            boolean start, boolean back);
}
