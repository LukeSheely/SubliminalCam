#include "app/image_loader.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace subliminalcam;

int main() {
  const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com_result)) return EXIT_FAILURE;
  const auto path = std::filesystem::temp_directory_path() / "subliminalcam-image-test.bmp";

  BITMAPFILEHEADER file_header{};
  BITMAPINFOHEADER info_header{};
  constexpr std::array<Pixel, 4> pixels{{
      {0, 0, 255, 255}, {0, 255, 0, 255},
      {255, 0, 0, 255}, {255, 255, 255, 255}}};
  file_header.bfType = 0x4d42;
  file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  file_header.bfSize = file_header.bfOffBits + sizeof(pixels);
  info_header.biSize = sizeof(BITMAPINFOHEADER);
  info_header.biWidth = 2;
  info_header.biHeight = 2;
  info_header.biPlanes = 1;
  info_header.biBitCount = 32;
  info_header.biCompression = BI_RGB;

  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    output.write(reinterpret_cast<const char*>(pixels.data()), sizeof(pixels));
  }

  const auto image = load_image(path);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  CoUninitialize();
  if (!image || image->width != 2 || image->height != 2 || image->pixels.size() != 4) {
    std::cerr << "Windows image decoder test failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "Windows image decoder test passed.\n";
  return EXIT_SUCCESS;
}
