#include <cstdint>

extern "C" {

    // Stub em.
    _declspec(dllexport) int32_t _XUsbcamCreate(...) {
        return -1;
    }

    _declspec(dllexport) int32_t _XUsbcamDestroy(...) {
        return -1;
    }

    _declspec(dllexport) int32_t _XUsbcamGetState(...) {
        return -1;
    }

    _declspec(dllexport) int32_t _XUsbcamSetConfig(...) {
        return -1;
    }

    _declspec(dllexport) int32_t _XUsbcamSetCaptureMode(...) {
        return -1;
    }

    _declspec(dllexport) int32_t _XUsbcamReadFrame(...) {
        return -1;
    }

}