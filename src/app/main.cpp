#include "app/camera_capture.h"
#include "app/camera_devices.h"
#include "core/compositor.h"
#include "core/diagnostics.h"
#include "core/settings.h"
#include "shared/frame_transport.h"

#include <mfapi.h>
#include <mferror.h>
#include <objbase.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace subliminalcam;

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

class PreviewWidget final : public QWidget {
 public:
  explicit PreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumSize(560, 315);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAccessibleName("Virtual camera preview");
  }

  void submit(std::shared_ptr<Frame> frame) {
    {
      std::scoped_lock lock(frame_mutex_);
      frame_ = std::move(frame);
    }
    if (!update_queued_.exchange(true)) {
      QPointer<PreviewWidget> safe(this);
      QMetaObject::invokeMethod(this, [safe] {
        if (!safe) return;
        safe->update_queued_ = false;
        safe->update();
      }, Qt::QueuedConnection);
    }
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#08090c"));
    std::shared_ptr<Frame> frame;
    {
      std::scoped_lock lock(frame_mutex_);
      frame = frame_;
    }

    const QRect canvas = rect().adjusted(2, 2, -2, -2);
    if (frame) {
      QImage image(reinterpret_cast<const uchar*>(frame->pixels.data()), frame->width,
                   frame->height, frame->width * static_cast<int>(sizeof(Pixel)),
                   QImage::Format_ARGB32);
      const QSize fitted = image.size().scaled(canvas.size(), Qt::KeepAspectRatio);
      const QRect destination(QPoint(canvas.center().x() - fitted.width() / 2,
                                     canvas.center().y() - fitted.height() / 2), fitted);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(destination, image);
      painter.setPen(QPen(QColor("#48cf79"), 2));
    } else {
      painter.setPen(QColor("#9da4b1"));
      painter.setFont(QFont("Segoe UI Variable Text", 11));
      painter.drawText(canvas, Qt::AlignCenter, "Waiting for a physical camera");
      painter.setPen(QPen(QColor("#414650"), 1));
    }
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(canvas, 12, 12);
  }

 private:
  std::mutex frame_mutex_;
  std::shared_ptr<Frame> frame_;
  std::atomic_bool update_queued_{false};
};

QFrame* card(QWidget* contents) {
  auto* frame = new QFrame;
  frame->setObjectName("card");
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setSpacing(9);
  layout->addWidget(contents);
  return frame;
}

class StudioWindow final : public QMainWindow {
 public:
  StudioWindow() {
    settings_ = load_settings(settings_path());
    setWindowTitle("SubliminalCam");
    setMinimumSize(760, 600);
    resize(960, 720);
    setStyleSheet(style_sheet());
    build_ui();
    load_controls();

    tick_timer_.setInterval(250);
    connect(&tick_timer_, &QTimer::timeout, this, [this] { tick(); });
    tick_timer_.start();

    DiagnosticLog::instance().write(DiagnosticLevel::info, L"Application",
                                     L"Controller initialized (version 0.5.0-simple)");
    capture_ = std::make_unique<CameraCapture>(
        [this](std::shared_ptr<Frame> frame) { process_frame(std::move(frame)); },
        [this](const CaptureStatus& status) {
          QMetaObject::invokeMethod(this, [this, status] { handle_capture_status(status); },
                                    Qt::QueuedConnection);
        });
    start_camera();
  }

  ~StudioWindow() override {
    shutting_down_ = true;
    if (capture_) capture_->stop();
  }

 private:
  static QString style_sheet() {
    return R"(
      * { font-family: "Segoe UI Variable Text", "Segoe UI"; font-size: 13px; }
      QMainWindow, QWidget#root { background: #121317; color: #f0f2f7; }
      QFrame#topBar, QFrame#statusBar { background: #1f2127; border: 0; }
      QFrame#card { background: #1e2026; border: 1px solid #2f323a; border-radius: 12px; }
      QLabel#appTitle { font-size: 20px; font-weight: 650; color: #f7f8fb; }
      QLabel#subtitle, QLabel#metric { color: #9ba2af; }
      QLabel#sectionLabel { color: #9ca3af; font-size: 11px; font-weight: 650; }
      QLabel#liveBadge, QLabel#pausedBadge, QLabel#waitingBadge, QLabel#errorBadge {
        border-radius: 11px; padding: 5px 10px; font-size: 11px; font-weight: 650; }
      QLabel#liveBadge { background: #1d412e; color: #71ec9c; border: 1px solid #2b6645; }
      QLabel#pausedBadge { background: #292c33; color: #abb1bd; border: 1px solid #454a55; }
      QLabel#waitingBadge { background: #463719; color: #ffc85c; border: 1px solid #775d28; }
      QLabel#errorBadge { background: #482125; color: #ff8a91; border: 1px solid #7b353b; }
      QComboBox, QLineEdit { background: #181a1f; color: #edf0f5; border: 1px solid #383c45;
        border-radius: 8px; padding: 8px 10px; selection-background-color: #254f8f; }
      QComboBox QAbstractItemView { background: #202229; border: 1px solid #414650; padding: 4px; }
      QCheckBox { color: #d7dae0; spacing: 7px; }
      QPushButton { background: #30333b; color: #f5f7fa; border: 1px solid #414650;
        border-radius: 8px; padding: 8px 13px; }
      QPushButton:hover { background: #3a3e48; }
      QPushButton:pressed { background: #252830; }
      QPushButton#messageToggle { background: #3f82e8; border-color: #5797f6; font-weight: 650; }
      QPushButton#messageToggle:checked { background: #9d343c; border-color: #ca5059; }
      QToolTip { background: #2a2d34; color: white; border: 1px solid #4b515d; padding: 6px; }
    )";
  }

  static QLabel* section_label(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName("sectionLabel");
    return label;
  }

  void build_ui() {
    auto* root = new QWidget;
    root->setObjectName("root");
    auto* shell = new QVBoxLayout(root);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    shell->addWidget(build_header());

    auto* body = new QWidget;
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(16, 14, 16, 14);
    body_layout->setSpacing(12);
    body_layout->addWidget(build_camera_controls());

    auto* program_heading = new QHBoxLayout;
    program_heading->addWidget(section_label("CAMERA OUTPUT"));
    program_heading->addStretch();
    metrics_ = new QLabel("OFFLINE");
    metrics_->setObjectName("metric");
    program_heading->addWidget(metrics_);
    body_layout->addLayout(program_heading);
    preview_ = new PreviewWidget;
    body_layout->addWidget(preview_, 1);
    body_layout->addWidget(build_message_controls());

    shell->addWidget(body, 1);
    shell->addWidget(build_status());
    setCentralWidget(root);
  }

  QWidget* build_header() {
    auto* bar = new QFrame;
    bar->setObjectName("topBar");
    bar->setFixedHeight(66);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 9, 20, 9);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel("SubliminalCam");
    title->setObjectName("appTitle");
    auto* subtitle = new QLabel("Camera passthrough + manual message overlay");
    subtitle->setObjectName("subtitle");
    titles->addWidget(title);
    titles->addWidget(subtitle);
    layout->addLayout(titles);
    layout->addStretch();

    output_button_ = new QPushButton("Stop camera output");
    output_button_->setCheckable(true);
    output_button_->setChecked(true);
    layout->addWidget(output_button_);
    live_badge_ = new QLabel("◌  STARTING");
    live_badge_->setObjectName("waitingBadge");
    layout->addWidget(live_badge_);
    connect(output_button_, &QPushButton::toggled, this, [this](bool enabled) {
      output_enabled_ = enabled;
      output_button_->setText(enabled ? "Stop camera output" : "Start camera output");
      if (!enabled) {
        std::scoped_lock lock(frame_writer_mutex_);
        frame_writer_.invalidate();
      }
      update_output_badge();
      show_status(enabled ? "Virtual camera output started" : "Virtual camera output paused");
    });
    return bar;
  }

  QWidget* build_camera_controls() {
    auto* content = new QWidget;
    auto* layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(9);
    layout->addWidget(section_label("CAMERA"));
    camera_combo_ = new QComboBox;
    camera_combo_->setAccessibleName("Physical camera");
    layout->addWidget(camera_combo_, 1);
    refresh_cameras_button_ = new QPushButton("Refresh");
    refresh_cameras_button_->setToolTip("Rescan after plugging in or reconnecting a camera");
    layout->addWidget(refresh_cameras_button_);
    mirror_ = new QCheckBox("Mirror");
    layout->addWidget(mirror_);

    connect(camera_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
      if (controls_loaded_) start_camera();
    });
    connect(refresh_cameras_button_, &QPushButton::clicked, this,
            [this] { refresh_cameras(); });
    connect(mirror_, &QCheckBox::toggled, this, [this](bool checked) {
      mirror_enabled_ = checked;
      settings_.mirror = checked;
      save_settings(settings_, settings_path());
    });
    return card(content);
  }

  QWidget* build_message_controls() {
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(9);
    layout->addWidget(section_label("MESSAGE"));
    auto* row = new QHBoxLayout;
    prompt_edit_ = new QLineEdit;
    prompt_edit_->setPlaceholderText("Type the message shown on camera…");
    prompt_edit_->setClearButtonEnabled(true);
    row->addWidget(prompt_edit_, 1);
    message_toggle_ = new QPushButton("Show message");
    message_toggle_->setObjectName("messageToggle");
    message_toggle_->setCheckable(true);
    message_toggle_->setMinimumWidth(140);
    row->addWidget(message_toggle_);
    layout->addLayout(row);
    auto* hint = new QLabel("The message switches on and off immediately—no timer, animation, or fade.");
    hint->setObjectName("subtitle");
    layout->addWidget(hint);

    connect(prompt_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
      {
        std::scoped_lock lock(message_mutex_);
        message_text_ = text.toStdWString();
      }
      if (text.trimmed().isEmpty() && message_toggle_->isChecked()) {
        message_toggle_->setChecked(false);
      }
    });
    connect(prompt_edit_, &QLineEdit::editingFinished, this, [this] { save_controls(); });
    connect(message_toggle_, &QPushButton::toggled, this, [this](bool visible) {
      message_visible_ = visible;
      message_toggle_->setText(visible ? "Hide message" : "Show message");
      show_status(visible ? "Message is visible on camera" : "Message hidden");
      save_controls();
    });
    return card(content);
  }

  QWidget* build_status() {
    auto* bar = new QFrame;
    bar->setObjectName("statusBar");
    bar->setFixedHeight(38);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 0, 20, 0);
    status_ = new QLabel("Frames stay local · Ready for video apps");
    layout->addWidget(status_);
    layout->addStretch();
    diagnostics_button_ = new QPushButton("Diagnostics");
    diagnostics_button_->setFixedHeight(28);
    layout->addWidget(diagnostics_button_);
    connect(diagnostics_button_, &QPushButton::clicked, this, [this] { show_diagnostics(); });
    return bar;
  }

  void load_controls() {
    int selected = 0;
    auto enumeration = enumerate_cameras_detailed();
    cameras_ = std::move(enumeration.devices);
    if (enumeration.excluded_virtual_devices) {
      DiagnosticLog::instance().write(DiagnosticLevel::info, L"Enumeration",
          L"Excluded SubliminalCam virtual output from the physical-camera picker");
    }
    if (FAILED(enumeration.result)) {
      DiagnosticLog::instance().write(DiagnosticLevel::error, L"Enumeration",
                                       L"MFEnumDeviceSources failed", enumeration.result);
    }
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
      camera_combo_->addItem(QString::fromStdWString(cameras_[i].name));
      if (cameras_[i].symbolic_link == settings_.camera_id) selected = static_cast<int>(i);
    }
    if (!cameras_.empty()) camera_combo_->setCurrentIndex(selected);
    mirror_->setChecked(settings_.mirror);
    mirror_enabled_ = settings_.mirror;
    prompt_edit_->setText(QString::fromStdWString(settings_.message));
    message_toggle_->setChecked(false);
    controls_loaded_ = true;
  }

  void save_controls() {
    settings_.message = prompt_edit_->text().toStdWString();
    settings_.mirror = mirror_->isChecked();
    save_settings(settings_, settings_path());
  }

  void start_camera() {
    if (!capture_ || camera_combo_->currentIndex() < 0 ||
        camera_combo_->currentIndex() >= static_cast<int>(cameras_.size())) {
      show_status("No physical camera is available", true, true);
      return;
    }
    settings_.camera_id = cameras_[static_cast<std::size_t>(camera_combo_->currentIndex())].symbolic_link;
    save_controls();
    show_status("Connecting camera…");
    capture_->start(settings_.camera_id);
  }

  void refresh_cameras() {
    const auto previous_id = settings_.camera_id;
    if (capture_) capture_->stop();
    const QSignalBlocker blocker(camera_combo_);
    camera_combo_->clear();
    auto enumeration = enumerate_cameras_detailed();
    cameras_ = std::move(enumeration.devices);
    int selected = 0;
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
      camera_combo_->addItem(QString::fromStdWString(cameras_[i].name));
      if (cameras_[i].symbolic_link == previous_id) selected = static_cast<int>(i);
    }
    if (cameras_.empty()) {
      DiagnosticLog::instance().write(DiagnosticLevel::error, L"Enumeration",
                                       L"Camera rescan returned no devices", MF_E_NOT_FOUND);
      show_status("No camera found. Reconnect it, then press Refresh.", true, true);
      update_output_badge();
      return;
    }
    camera_combo_->setCurrentIndex(selected);
    start_camera();
  }

  void handle_capture_status(const CaptureStatus& status) {
    last_capture_status_ = status;
    update_output_badge();
    if (status.stage == CaptureStage::streaming) {
      show_status("Physical camera streaming");
    } else if (status.stage == CaptureStage::failed) {
      show_status(QString("Camera failed: %1 · %2")
                      .arg(QString::fromStdWString(status.detail))
                      .arg(QString::fromStdWString(hresult_message(status.result))), true, true);
    }
  }

  void update_output_badge() {
    QString text;
    QString style;
    if (!output_enabled_) {
      text = "○  PAUSED";
      style = "pausedBadge";
    } else if (last_capture_status_.stage == CaptureStage::failed || cameras_.empty()) {
      text = "!  NO CAMERA";
      style = "errorBadge";
    } else if (last_capture_status_.stage == CaptureStage::streaming) {
      text = "●  CAMERA LIVE";
      style = "liveBadge";
    } else {
      text = "◌  STARTING";
      style = "waitingBadge";
    }
    live_badge_->setText(text);
    live_badge_->setObjectName(style);
    live_badge_->style()->unpolish(live_badge_);
    live_badge_->style()->polish(live_badge_);
  }

  QString diagnostic_report() const {
    QString report;
    QTextStream output(&report);
    const auto capture = capture_ ? capture_->snapshot() : CaptureSnapshot{};
    const auto current = GetTickCount64();
    output << "SubliminalCam diagnostic report\n"
           << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n"
           << "App version: 0.5.0-simple\n"
           << "Windows: " << QSysInfo::prettyProductName() << " ("
           << QSysInfo::currentCpuArchitecture() << ")\n"
           << "Log: " << QString::fromStdWString(diagnostic_log_path().wstring()) << "\n\n"
           << "CAMERAS\nEnumerated: " << cameras_.size() << "\n";
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
      output << (static_cast<int>(i) == camera_combo_->currentIndex() ? "* " : "  ")
             << QString::fromStdWString(cameras_[i].name) << "\n  "
             << QString::fromStdWString(cameras_[i].symbolic_link) << "\n";
    }
    output << "\nCAPTURE\n"
           << "Stage: " << QString::fromWCharArray(capture_stage_name(capture.stage)) << "\n"
           << "Last result: " << QString::fromStdWString(hresult_message(capture.last_result)) << "\n"
           << "Frames received: " << capture.frames_received << "\n"
           << "Copy failures: " << capture.copy_failures << "\n"
           << "Empty stream ticks: " << capture.stream_ticks << "\n"
           << "Media-type changes: " << capture.media_type_changes << "\n"
           << "Format: " << capture.width << "x" << capture.height << " @ "
           << capture.frame_rate << " FPS\n"
           << "Last frame age: ";
    if (capture.last_frame_tick_ms == 0) output << "never\n";
    else output << (current - capture.last_frame_tick_ms) << " ms\n";
    output << "\nOUTPUT\n"
           << "Enabled: " << (output_enabled_.load() ? "yes" : "no") << "\n"
           << "Message visible: " << (message_visible_.load() ? "yes" : "no") << "\n"
           << "Frames rendered: " << rendered_frames_.load() << "\n"
           << "Last processing time: " << processing_us_.load() / 1000.0 << " ms\n"
           << "Frames published: " << published_frames_.load() << "\n"
           << "Publish failures: " << publish_failures_.load() << "\n"
           << "\nRECENT LOG\n";
    for (const auto& line : DiagnosticLog::instance().recent()) {
      output << QString::fromStdWString(line) << "\n";
    }
    return report;
  }

  void show_diagnostics() {
    QDialog dialog(this);
    dialog.setWindowTitle("SubliminalCam Diagnostics");
    dialog.resize(820, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* report = new QPlainTextEdit;
    report->setReadOnly(true);
    report->setLineWrapMode(QPlainTextEdit::NoWrap);
    report->setPlainText(diagnostic_report());
    layout->addWidget(report, 1);
    auto* actions = new QHBoxLayout;
    auto* refresh = new QPushButton("Refresh report");
    auto* restart = new QPushButton("Restart camera");
    auto* copy = new QPushButton("Copy");
    auto* save = new QPushButton("Save…");
    auto* close = new QPushButton("Close");
    actions->addWidget(refresh);
    actions->addWidget(restart);
    actions->addStretch();
    actions->addWidget(copy);
    actions->addWidget(save);
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(refresh, &QPushButton::clicked, &dialog,
            [this, report] { report->setPlainText(diagnostic_report()); });
    connect(restart, &QPushButton::clicked, &dialog, [this, report] {
      start_camera();
      report->setPlainText(diagnostic_report());
    });
    connect(copy, &QPushButton::clicked, &dialog,
            [report] { QApplication::clipboard()->setText(report->toPlainText()); });
    connect(save, &QPushButton::clicked, &dialog, [this, report] {
      const auto path = QFileDialog::getSaveFileName(
          this, "Save diagnostic report", "SubliminalCam-diagnostics.txt", "Text files (*.txt)");
      if (path.isEmpty()) return;
      QFile file(path);
      if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(report->toPlainText().toUtf8());
      }
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
  }

  void show_status(const QString& message, bool error = false, bool persistent = false) {
    status_->setText(message);
    status_->setStyleSheet(error ? "color:#ff737a" : "color:#f0f2f7");
    status_reset_at_ = persistent ? 0 : now_ms() + 2600;
  }

  void tick() {
    const auto current = now_ms();
    if (current - last_stats_at_ >= 1000) {
      const auto frames = rendered_frames_.load();
      const auto elapsed = std::max<std::int64_t>(1, current - last_stats_at_);
      const int fps = static_cast<int>((frames - last_stats_frames_) * 1000 / elapsed);
      metrics_->setText(QString("%1 FPS · %2 × %3 · %4 ms")
                            .arg(fps).arg(last_frame_width_.load())
                            .arg(last_frame_height_.load())
                            .arg(processing_us_.load() / 1000.0, 0, 'f', 1));
      last_stats_frames_ = frames;
      last_stats_at_ = current;
    }
    if (status_reset_at_ && current >= status_reset_at_) {
      status_reset_at_ = 0;
      status_->setStyleSheet({});
      status_->setText("Frames stay local · Ready for video apps");
    }
  }

  void process_frame(std::shared_ptr<Frame> frame) {
    if (!frame) return;
    const auto started = std::chrono::steady_clock::now();
    last_frame_width_ = frame->width;
    last_frame_height_ = frame->height;
    if (mirror_enabled_) mirror_horizontal(*frame);
    if (message_visible_) {
      std::wstring message;
      {
        std::scoped_lock lock(message_mutex_);
        message = message_text_;
      }
      draw_message(*frame, message);
    }
    if (output_enabled_) {
      std::scoped_lock lock(frame_writer_mutex_);
      if (frame_writer_.write(*frame, static_cast<std::uint64_t>(now_ms()) * 10000u)) {
        ++published_frames_;
      } else {
        const auto failures = ++publish_failures_;
        if (failures == 1 || failures % 60 == 0) {
          DiagnosticLog::instance().write(DiagnosticLevel::error, L"Transport",
                                           L"Failed to publish camera frame");
        }
      }
    }
    if (!shutting_down_) preview_->submit(std::move(frame));
    processing_us_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    ++rendered_frames_;
  }

  static void draw_message(Frame& frame, const std::wstring& message) {
    if (message.empty()) return;
    QImage image(reinterpret_cast<uchar*>(frame.pixels.data()), frame.width, frame.height,
                 frame.width * static_cast<int>(sizeof(Pixel)), QImage::Format_ARGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(frame.width / 10.0, frame.height / 2.0 - 52,
                     frame.width * 4.0 / 5.0, 104);
    painter.setPen(QPen(QColor(255, 255, 255, 105), 1));
    painter.setBrush(QColor(12, 14, 18, 220));
    painter.drawRoundedRect(box, 14, 14);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI Variable Display", 28, QFont::DemiBold));
    painter.drawText(box.adjusted(20, 8, -20, -8), Qt::AlignCenter | Qt::TextWordWrap,
                     QString::fromStdWString(message));
  }

  AppSettings settings_;
  SharedFrameWriter frame_writer_;
  std::mutex frame_writer_mutex_;
  std::unique_ptr<CameraCapture> capture_;
  std::vector<CameraDevice> cameras_;
  std::mutex message_mutex_;
  std::wstring message_text_;
  std::atomic_bool message_visible_{false};
  std::atomic_bool mirror_enabled_{true};
  std::atomic_bool shutting_down_{false};
  std::atomic_bool output_enabled_{true};
  std::atomic_uint64_t rendered_frames_{0};
  std::atomic_uint64_t processing_us_{0};
  std::atomic_int last_frame_width_{};
  std::atomic_int last_frame_height_{};
  std::atomic_uint64_t published_frames_{0};
  std::atomic_uint64_t publish_failures_{0};
  std::uint64_t last_stats_frames_{};
  std::int64_t last_stats_at_{now_ms()};
  std::int64_t status_reset_at_{};
  bool controls_loaded_{};
  CaptureStatus last_capture_status_;
  QTimer tick_timer_;
  PreviewWidget* preview_{};
  QComboBox* camera_combo_{};
  QPushButton* refresh_cameras_button_{};
  QCheckBox* mirror_{};
  QLineEdit* prompt_edit_{};
  QPushButton* message_toggle_{};
  QLabel* metrics_{};
  QLabel* status_{};
  QPushButton* diagnostics_button_{};
  QLabel* live_badge_{};
  QPushButton* output_button_{};
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("SubliminalCam");
  QApplication::setOrganizationName("SubliminalCam");
  QApplication::setStyle("Fusion");

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result)) return 2;
  const HRESULT mf_result = MFStartup(MF_VERSION);
  if (FAILED(mf_result)) {
    CoUninitialize();
    return 3;
  }

  int result = 0;
  {
    StudioWindow window;
    window.show();
    result = app.exec();
  }
  MFShutdown();
  CoUninitialize();
  return result;
}
