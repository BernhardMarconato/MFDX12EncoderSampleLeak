#pragma once
#include <windows.h>
#include <unknwn.h>
#include <wil/cppwinrt.h> // must be before the first C++ WinRT header
#include <wil/result.h>
#include <wil/stl.h>
#include <wil/resource.h>
#include <windows.media.core.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <ks.h>
#include <ksmedia.h>

// Undefine GetCurrentTime macro to prevent
// conflict with Storyboard::GetCurrentTime
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <wil/cppwinrt_helpers.h>

#pragma comment(lib, "mf")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "Wmcodecdspuuid")

#define INITGUID