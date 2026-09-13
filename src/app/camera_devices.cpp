#include "app/camera_devices.h"

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <string_view>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

CameraEnumerationResult enumerate_cameras_detailed() {
  CameraEnumerationResult result;
  ComPtr<IMFAttributes> attributes;
  result.result = MFCreateAttributes(&attributes, 1);
  if (FAILED(result.result)) return result;
  result.result = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(result.result)) return result;

  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  result.result = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(result.result)) return result;
  for (UINT32 index = 0; index < count; ++index) {
    wchar_t* name = nullptr;
    wchar_t* link = nullptr;
    UINT32 name_length = 0;
    UINT32 link_length = 0;
    if (SUCCEEDED(devices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                     &name, &name_length)) &&
        SUCCEEDED(devices[index]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &link_length))) {
      const std::wstring_view friendly_name{name};
      const std::wstring_view symbolic_link{link};
      const bool is_own_virtual_camera =
          friendly_name.starts_with(L"SubliminalCam") &&
          symbolic_link.find(L"vcamdevapi") != std::wstring_view::npos;
      if (is_own_virtual_camera) ++result.excluded_virtual_devices;
      else result.devices.push_back({name, link});
    }
    CoTaskMemFree(name);
    CoTaskMemFree(link);
    devices[index]->Release();
  }
  CoTaskMemFree(devices);
  return result;
}

std::vector<CameraDevice> enumerate_cameras() {
  return enumerate_cameras_detailed().devices;
}

}  // namespace subliminalcam
