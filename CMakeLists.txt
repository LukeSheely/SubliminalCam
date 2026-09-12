cmake_minimum_required(VERSION 3.24)
project(SubliminalCam VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "SubliminalCam currently supports Windows 11 only")
endif()

include(GNUInstallDirs)
# The Inno Setup layout keeps the controller and its Qt runtime together at the
# application root so Windows can resolve the DLLs without modifying PATH.
set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Runtime install directory" FORCE)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

add_library(subliminalcam_core STATIC
  src/core/compositor.cpp
  src/core/prompt_scheduler.cpp
  src/core/settings.cpp
  src/shared/frame_transport.cpp
)
target_include_directories(subliminalcam_core PUBLIC src)
target_compile_definitions(subliminalcam_core PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(subliminalcam_core PUBLIC advapi32)

qt_add_executable(SubliminalCam WIN32
  src/app/main.cpp
  src/app/camera_devices.cpp
  src/app/camera_capture.cpp
  src/app/image_loader.cpp
)
target_link_libraries(SubliminalCam PRIVATE
  subliminalcam_core
  Qt6::Core Qt6::Gui Qt6::Widgets
  mfplat mf mfreadwrite mfuuid ole32 windowscodecs shlwapi
)
target_compile_definitions(SubliminalCam PRIVATE UNICODE _UNICODE NOMINMAX)
if(MSVC)
  target_compile_options(SubliminalCam PRIVATE /utf-8)
endif()

add_executable(SubliminalCamVcamCtl src/vcamctl/main.cpp)
target_link_libraries(SubliminalCamVcamCtl PRIVATE
  mfplat mfuuid mfsensorgroup ole32 advapi32
)
target_compile_definitions(SubliminalCamVcamCtl PRIVATE UNICODE _UNICODE NOMINMAX)

add_executable(subliminalcam_tests tests/core_tests.cpp)
target_link_libraries(subliminalcam_tests PRIVATE subliminalcam_core)
target_compile_definitions(subliminalcam_tests PRIVATE UNICODE _UNICODE NOMINMAX)

enable_testing()
add_test(NAME core COMMAND subliminalcam_tests)

set(VCAM_SOURCE_DLL "${CMAKE_SOURCE_DIR}/vendor/windows-camera/VirtualCameraMediaSource/x64/Release/VirtualCameraMediaSource.dll")
if(EXISTS "${VCAM_SOURCE_DLL}")
  add_executable(vcam_source_tests tests/vcam_source_tests.cpp)
  target_link_libraries(vcam_source_tests PRIVATE mfplat mfreadwrite mfuuid ole32)
  target_compile_definitions(vcam_source_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  add_test(NAME vcam_source COMMAND vcam_source_tests "${VCAM_SOURCE_DLL}")
  set_tests_properties(vcam_source PROPERTIES TIMEOUT 10)
endif()

install(TARGETS SubliminalCam SubliminalCamVcamCtl RUNTIME DESTINATION .)
qt_generate_deploy_app_script(
  TARGET SubliminalCam
  OUTPUT_SCRIPT subliminalcam_deploy_script
  NO_TRANSLATIONS
)
install(SCRIPT ${subliminalcam_deploy_script})
install(FILES
  "${VCAM_SOURCE_DLL}"
  DESTINATION . OPTIONAL
)
install(FILES LICENSE README.md ROADMAP.md TROUBLESHOOTING.md THIRD_PARTY_NOTICES.md DESTINATION .)
