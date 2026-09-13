#include "app/image_loader.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

std::optional<Frame> load_image(const std::filesystem::path& path, int max_dimension) {
  ComPtr<IWICImagingFactory> factory;
  HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory));
  ComPtr<IWICBitmapDecoder> decoder;
  if (SUCCEEDED(result)) {
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
  }
  ComPtr<IWICBitmapFrameDecode> source;
  if (SUCCEEDED(result)) result = decoder->GetFrame(0, &source);

  UINT width = 0;
  UINT height = 0;
  if (SUCCEEDED(result)) result = source->GetSize(&width, &height);
  if (FAILED(result) || width == 0 || height == 0) return std::nullopt;

  IWICBitmapSource* bitmap = source.Get();
  ComPtr<IWICBitmapScaler> scaler;
  const UINT largest = std::max(width, height);
  if (max_dimension > 0 && largest > static_cast<UINT>(max_dimension)) {
    const double scale = static_cast<double>(max_dimension) / largest;
    width = std::max<UINT>(1, static_cast<UINT>(width * scale));
    height = std::max<UINT>(1, static_cast<UINT>(height * scale));
    result = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(result)) {
      result = scaler->Initialize(source.Get(), width, height, WICBitmapInterpolationModeFant);
    }
    if (FAILED(result)) return std::nullopt;
    bitmap = scaler.Get();
  }

  ComPtr<IWICFormatConverter> converter;
  result = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(result)) {
    result = converter->Initialize(bitmap, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
  }
  if (FAILED(result)) return std::nullopt;

  Frame frame(static_cast<int>(width), static_cast<int>(height));
  result = converter->CopyPixels(nullptr, width * static_cast<UINT>(sizeof(Pixel)),
      static_cast<UINT>(frame.pixels.size() * sizeof(Pixel)),
      reinterpret_cast<BYTE*>(frame.pixels.data()));
  if (FAILED(result)) return std::nullopt;
  return frame;
}

}  // namespace subliminalcam
