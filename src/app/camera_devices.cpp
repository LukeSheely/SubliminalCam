#include "app/camera_devices.h"

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

std::vector<CameraDevice> enumerate_cameras() {
  std::vector<CameraDevice> result;
  ComPtr<IMFAttributes> attributes;
  if (FAILED(MFCreateAttributes(&attributes, 1))) return result;
  if (FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return result;

  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &count))) return result;
  for (UINT32 index = 0; index < count; ++index) {
    wchar_t* name = nullptr;
    wchar_t* link = nullptr;
    UINT32 name_length = 0;
    UINT32 link_length = 0;
    if (SUCCEEDED(devices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                     &name, &name_length)) &&
        SUCCEEDED(devices[index]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &link_length))) {
      result.push_back({name, link});
    }
    CoTaskMemFree(name);
    CoTaskMemFree(link);
    devices[index]->Release();
  }
  CoTaskMemFree(devices);
  return result;
}

}  // namespace subliminalcam
