package com.rex.bo2;

public class NativeBridge {
    private MainActivity mActivity;

    public NativeBridge(MainActivity activity) {
        mActivity = activity;
    }

    public void register() {
        registerBridge(this);
    }

    // Called from C++ via bo2::RequestAndroidRestart
    public void requestRestart(String app, String mode) {
        mActivity.restartWith(app, mode);
    }

    private native void registerBridge(NativeBridge bridge);
}
