#include "app/camera_devices.h"
#include "app/camera_capture.h"
#include "app/image_loader.h"
#include "core/compositor.h"
#include "core/prompt_scheduler.h"
#include "core/settings.h"
#include "shared/frame_transport.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mfapi.h>
#include <shlwapi.h>

#include <chrono>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace subliminalcam;

constexpr wchar_t kWindowClass[] = L"SubliminalCam.MainWindow";
constexpr UINT_PTR kUiTimer = 1;
constexpr int kCameraCombo = 100;
constexpr int kPromptEdit = 101;
constexpr int kPromptsEnabled = 102;
constexpr int kShowNow = 103;
constexpr int kBackgroundCombo = 104;
constexpr int kSave = 105;

struct AppState {
  AppSettings settings;
  PromptScheduler scheduler;
  PromptState prompt;
  std::vector<CameraDevice> cameras;
  HWND camera_combo{};
  HWND prompt_edit{};
  HWND enabled_check{};
  HWND background_combo{};
  HWND window{};
  std::mutex frame_mutex;
  std::mutex prompt_mutex;
  std::mutex background_mutex;
  std::shared_ptr<Frame> latest_frame;
  std::unique_ptr<CameraCapture> capture;
  std::atomic_bool mirror{true};
  std::atomic_int background_mode{static_cast<int>(BackgroundMode::none)};
  std::atomic_int blur_radius{12};
  SharedFrameWriter frame_writer;
  std::shared_ptr<Frame> custom_background;
};

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void set_font(HWND control) {
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

HWND add_control(HWND parent, const wchar_t* klass, const wchar_t* text, DWORD style,
                 int x, int y, int width, int height, int id) {
  auto result = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style,
                                x, y, width, height, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
  set_font(result);
  return result;
}

void sync_settings_from_ui(AppState& state) {
  wchar_t buffer[512]{};
  GetWindowTextW(state.prompt_edit, buffer, static_cast<int>(std::size(buffer)));
  state.settings.prompts.messages = {buffer};
  state.settings.prompts.enabled = Button_GetCheck(state.enabled_check) == BST_CHECKED;
  const auto background = ComboBox_GetCurSel(state.background_combo);
  state.settings.background_mode = background < 0 ? BackgroundMode::none
                                                   : static_cast<BackgroundMode>(background);
  state.settings = sanitize(std::move(state.settings));
  state.background_mode = static_cast<int>(state.settings.background_mode);
  state.blur_radius = state.settings.blur_radius;
  state.scheduler.configure(state.settings.prompts, now_ms());
}

void render_prompt_overlay(Frame& frame, const PromptState& prompt) {
  if (!prompt.disclosure_visible && !prompt.prompt_visible) return;
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = frame.width;
  info.bmiHeader.biHeight = -frame.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bitmap_pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bitmap_pixels, nullptr, 0);
  if (!bitmap || !bitmap_pixels) {
    DeleteDC(dc);
    return;
  }
  const auto old_bitmap = SelectObject(dc, bitmap);
  std::memcpy(bitmap_pixels, frame.pixels.data(), frame.pixels.size() * sizeof(Pixel));
  SetBkMode(dc, TRANSPARENT);

  if (prompt.disclosure_visible) {
    RECT badge{18, 18, 190, 52};
    HBRUSH brush = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(dc, &badge, brush);
    DeleteObject(brush);
    SetTextColor(dc, RGB(255, 214, 102));
    auto font = CreateFontW(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    DrawTextW(dc, L"Prompts enabled", -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
    DeleteObject(font);
  }

  if (prompt.prompt_visible) {
    RECT box{frame.width / 8, frame.height / 2 - 58, frame.width * 7 / 8, frame.height / 2 + 58};
    HBRUSH brush = CreateSolidBrush(RGB(247, 248, 250));
    FillRect(dc, &box, brush);
    DeleteObject(brush);
    SetTextColor(dc, RGB(17, 23, 32));
    auto font = CreateFontW(42, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    DrawTextW(dc, prompt.message.c_str(), -1, &box,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
    DeleteObject(font);
  }
  std::memcpy(frame.pixels.data(), bitmap_pixels, frame.pixels.size() * sizeof(Pixel));
  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
}

Frame apply_selected_effect(Frame frame, BackgroundMode mode, int blur_radius,
                            const std::shared_ptr<Frame>& custom_background) {
  switch (mode) {
    case BackgroundMode::blur:
      return box_blur(frame, blur_radius);
    case BackgroundMode::solid:
      return solid_background(frame.width, frame.height, Pixel{46, 35, 27, 255});
    case BackgroundMode::gradient:
      return gradient_background(frame.width, frame.height,
                                 Pixel{58, 40, 24, 255}, Pixel{24, 14, 10, 255});
    case BackgroundMode::image:
      if (custom_background && custom_background->width == frame.width &&
          custom_background->height == frame.height) return *custom_background;
      return gradient_background(frame.width, frame.height,
                                 Pixel{70, 48, 32, 255}, Pixel{26, 19, 16, 255});
    case BackgroundMode::none:
    default:
      return frame;
  }
}

void choose_background_image(HWND window, AppState& state) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW dialog{sizeof(dialog)};
  dialog.hwndOwner = window;
  dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All files\0*.*\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&dialog)) return;
  auto loaded = load_image(path, 1280, 720);
  if (!loaded) {
    MessageBoxW(window, L"The selected image could not be decoded.", L"SubliminalCam",
                MB_OK | MB_ICONERROR);
    return;
  }
  {
    std::scoped_lock lock(state.background_mutex);
    state.custom_background = std::make_shared<Frame>(std::move(*loaded));
  }
  state.settings.image_path = path;
}

void start_selected_camera(AppState& state) {
  const auto selected = ComboBox_GetCurSel(state.camera_combo);
  if (selected < 0 || static_cast<std::size_t>(selected) >= state.cameras.size()) return;
  state.settings.camera_id = state.cameras[static_cast<std::size_t>(selected)].symbolic_link;
  state.capture->start(state.settings.camera_id);
}

void paint_preview(HWND window, AppState& state) {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(window, &ps);
  RECT client{};
  GetClientRect(window, &client);
  RECT preview{24, 194, client.right - 24, client.bottom - 24};

  std::shared_ptr<Frame> frame;
  {
    std::scoped_lock lock(state.frame_mutex);
    frame = state.latest_frame;
  }
  if (frame) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = frame->width;
    info.bmiHeader.biHeight = -frame->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, preview.left, preview.top, preview.right - preview.left, preview.bottom - preview.top,
                  0, 0, frame->width, frame->height, frame->pixels.data(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
  } else {
    TRIVERTEX vertices[2] = {
        {preview.left, preview.top, 0x2200, 0x2c00, 0x4100, 0xff00},
        {preview.right, preview.bottom, 0x0b00, 0x1200, 0x2200, 0xff00}};
    GRADIENT_RECT gradient{0, 1};
    GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_V);
  }

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(220, 226, 238));
  auto old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  RECT hint = preview;
  hint.top += 18;
  DrawTextW(dc, frame ? L"Live local preview" : L"Waiting for a physical camera",
            -1, &hint, DT_CENTER | DT_TOP | DT_SINGLELINE);

  PromptState prompt;
  {
    std::scoped_lock lock(state.prompt_mutex);
    prompt = state.prompt;
  }
  if (!frame && prompt.disclosure_visible) {
    RECT badge{preview.left + 16, preview.top + 16, preview.left + 140, preview.top + 44};
    HBRUSH brush = CreateSolidBrush(RGB(20, 20, 22));
    FillRect(dc, &badge, brush);
    DeleteObject(brush);
    SetTextColor(dc, RGB(255, 214, 102));
    DrawTextW(dc, L"Prompts enabled", -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  if (!frame && prompt.prompt_visible) {
    RECT box{preview.left + 80, preview.top + (preview.bottom - preview.top) / 2 - 40,
             preview.right - 80, preview.top + (preview.bottom - preview.top) / 2 + 40};
    HBRUSH brush = CreateSolidBrush(RGB(245, 247, 250));
    FillRect(dc, &box, brush);
    DeleteObject(brush);
    SetTextColor(dc, RGB(20, 24, 31));
    DrawTextW(dc, prompt.message.c_str(), -1, &box,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  SelectObject(dc, old_font);
  EndPaint(window, &ps);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    state = static_cast<AppState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }

  switch (message) {
    case WM_CREATE: {
      add_control(window, L"STATIC", L"Physical camera", 0, 24, 20, 150, 20, 0);
      state->camera_combo = add_control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                        24, 42, 330, 180, kCameraCombo);
      state->cameras = enumerate_cameras();
      for (const auto& camera : state->cameras) ComboBox_AddString(state->camera_combo, camera.name.c_str());
      if (!state->cameras.empty()) ComboBox_SetCurSel(state->camera_combo, 0);
      state->window = window;
      state->mirror = state->settings.mirror;
      state->background_mode = static_cast<int>(state->settings.background_mode);
      state->blur_radius = state->settings.blur_radius;
      if (!state->settings.image_path.empty()) {
        if (auto loaded = load_image(state->settings.image_path, 1280, 720))
          state->custom_background = std::make_shared<Frame>(std::move(*loaded));
      }
      state->capture = std::make_unique<CameraCapture>([state](Frame frame) {
        if (state->mirror) mirror_horizontal(frame);
        std::shared_ptr<Frame> custom_background;
        {
          std::scoped_lock lock(state->background_mutex);
          custom_background = state->custom_background;
        }
        frame = apply_selected_effect(std::move(frame),
            static_cast<BackgroundMode>(state->background_mode.load()), state->blur_radius.load(),
            custom_background);
        PromptState prompt;
        {
          std::scoped_lock lock(state->prompt_mutex);
          prompt = state->prompt;
        }
        render_prompt_overlay(frame, prompt);
        state->frame_writer.write(frame, static_cast<std::uint64_t>(now_ms()) * 10000u);
        {
          std::scoped_lock lock(state->frame_mutex);
          state->latest_frame = std::make_shared<Frame>(std::move(frame));
        }
        InvalidateRect(state->window, nullptr, FALSE);
      });
      start_selected_camera(*state);

      add_control(window, L"STATIC", L"Background", 0, 374, 20, 150, 20, 0);
      state->background_combo = add_control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                            374, 42, 180, 180, kBackgroundCombo);
      for (const auto* item : {L"None", L"Background blur", L"Solid color", L"Gradient", L"Image"})
        ComboBox_AddString(state->background_combo, item);
      ComboBox_SetCurSel(state->background_combo, static_cast<int>(state->settings.background_mode));

      add_control(window, L"STATIC", L"Disclosed prompt", 0, 24, 84, 150, 20, 0);
      const auto initial = state->settings.prompts.messages.empty() ? L"" : state->settings.prompts.messages.front().c_str();
      state->prompt_edit = add_control(window, L"EDIT", initial, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                       24, 106, 530, 28, kPromptEdit);
      state->enabled_check = add_control(window, L"BUTTON", L"Prompts enabled (disclosed in output)",
                                         BS_AUTOCHECKBOX | WS_TABSTOP, 24, 146, 260, 28, kPromptsEnabled);
      Button_SetCheck(state->enabled_check, state->settings.prompts.enabled ? BST_CHECKED : BST_UNCHECKED);
      add_control(window, L"BUTTON", L"Show now", BS_PUSHBUTTON | WS_TABSTOP,
                  300, 146, 110, 28, kShowNow);
      add_control(window, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP,
                  424, 146, 130, 28, kSave);
      state->scheduler.configure(state->settings.prompts, now_ms());
      SetTimer(window, kUiTimer, 33, nullptr);
      return 0;
    }
    case WM_COMMAND:
      if (!state) break;
      if (LOWORD(wparam) == kCameraCombo && HIWORD(wparam) == CBN_SELCHANGE) {
        start_selected_camera(*state);
        return 0;
      }
      if (LOWORD(wparam) == kBackgroundCombo && HIWORD(wparam) == CBN_SELCHANGE) {
        if (ComboBox_GetCurSel(state->background_combo) == static_cast<int>(BackgroundMode::image))
          choose_background_image(window, *state);
        sync_settings_from_ui(*state);
        return 0;
      }
      if (LOWORD(wparam) == kShowNow) {
        sync_settings_from_ui(*state);
        state->scheduler.show_now(now_ms());
        {
          std::scoped_lock lock(state->prompt_mutex);
          state->prompt = state->scheduler.tick(now_ms());
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (LOWORD(wparam) == kSave) {
        sync_settings_from_ui(*state);
        save_settings(state->settings, settings_path());
        MessageBoxW(window, L"Settings saved locally.", L"SubliminalCam", MB_OK | MB_ICONINFORMATION);
        return 0;
      }
      break;
    case WM_TIMER:
      if (state && wparam == kUiTimer) {
        const auto next = state->scheduler.tick(now_ms());
        PromptState previous;
        {
          std::scoped_lock lock(state->prompt_mutex);
          previous = state->prompt;
        }
        if (next.disclosure_visible != previous.disclosure_visible ||
            next.prompt_visible != previous.prompt_visible || next.message != previous.message) {
          {
            std::scoped_lock lock(state->prompt_mutex);
            state->prompt = next;
          }
          InvalidateRect(window, nullptr, FALSE);
        }
      }
      return 0;
    case WM_PAINT:
      if (state) paint_preview(window, *state);
      return 0;
    case WM_DESTROY:
      KillTimer(window, kUiTimer);
      if (state && state->capture) state->capture->stop();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    CoUninitialize();
    return 2;
  }
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);

  AppState state;
  state.settings = load_settings(settings_path());
  WNDCLASSEXW klass{sizeof(klass)};
  klass.lpfnWndProc = window_proc;
  klass.hInstance = instance;
  klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  klass.lpszClassName = kWindowClass;
  klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassExW(&klass);

  HWND window = CreateWindowExW(0, kWindowClass, L"SubliminalCam · consent-based virtual camera",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 620,
                                nullptr, nullptr, instance, &state);
  if (!window) {
    MFShutdown();
    CoUninitialize();
    return 3;
  }
  ShowWindow(window, show);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  MFShutdown();
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
