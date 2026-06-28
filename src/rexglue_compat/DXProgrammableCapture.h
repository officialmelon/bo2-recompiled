#pragma once

#include <unknwn.h>

#ifndef __IDXGraphicsAnalysis_INTERFACE_DEFINED__
#define __IDXGraphicsAnalysis_INTERFACE_DEFINED__

struct __declspec(uuid("9f251514-9d4d-4902-9d60-18988ab7d4b5")) IDXGraphicsAnalysis
    : public IUnknown {
  virtual void STDMETHODCALLTYPE BeginCapture() = 0;
  virtual void STDMETHODCALLTYPE EndCapture() = 0;
};

#endif  // __IDXGraphicsAnalysis_INTERFACE_DEFINED__
