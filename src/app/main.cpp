#include "app/camera_capture.h"
#include "app/camera_devices.h"
#include "app/image_loader.h"
#include "core/compositor.h"
#include "core/prompt_scheduler.h"
#include "core/settings.h"
#include "shared/frame_transport.h"

#include <mfapi.h>
#include <objbase.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMetaObject>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
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
    setMinimumSize(420, 236);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAccessibleName("Program output preview");
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
    painter.setRenderHint(QPainter::Antialiasing);
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
    painter.drawRoundedRect(canvas, 10, 10);
  }

 private:
  std::mutex frame_mutex_;
  std::shared_ptr<Frame> frame_;
  std::atomic_bool update_queued_{false};
};

QFrame* card(QWidget* contents, QWidget* parent = nullptr) {
  auto* frame = new QFrame(parent);
  frame->setObjectName("card");
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(14, 12, 14, 14);
  layout->setSpacing(8);
  layout->addWidget(contents);
  return frame;
}

QLabel* section_label(const QString& text) {
  auto* label = new QLabel(text);
  label->setObjectName("sectionLabel");
  return label;
}

class StudioWindow final : public QMainWindow {
 public:
  StudioWindow() {
    settings_ = load_settings(settings_path());
    setWindowTitle("SubliminalCam Studio");
    setMinimumSize(1040, 680);
    resize(1280, 780);
    setStyleSheet(style_sheet());
    build_ui();
    load_controls();

    scheduler_.configure(settings_.prompts, now_ms());
    tick_timer_.setInterval(50);
    connect(&tick_timer_, &QTimer::timeout, this, [this] { tick(); });
    tick_timer_.start();

    capture_ = std::make_unique<CameraCapture>(
        [this](std::shared_ptr<Frame> frame) { process_frame(std::move(frame)); });
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
      QFrame#topBar, QFrame#statusBar { background: rgba(31, 33, 39, 235); border: 0; }
      QFrame#card { background: #1e2026; border: 1px solid #2b2e36; border-radius: 12px; }
      QLabel#appTitle { font-size: 20px; font-weight: 650; letter-spacing: -0.3px; color: #f7f8fb; }
      QLabel#subtitle, QLabel#metric, QLabel#statusText { color: #9da4b1; }
      QLabel#sectionLabel { color: #9da4b1; font-size: 11px; font-weight: 700; letter-spacing: 1px; }
      QLabel#programLabel { font-size: 12px; font-weight: 700; letter-spacing: 1px; color: #e6e9ef; }
      QLabel#liveBadge { background: #1d412e; color: #71ec9c; border: 1px solid #2b6645;
                         border-radius: 14px; padding: 6px 12px; font-size: 11px;
                         font-weight: 700; letter-spacing: .6px; }
      QLabel#pausedBadge { background: #292c33; color: #abb1bd; border: 1px solid #454a55;
                           border-radius: 14px; padding: 6px 12px; font-size: 11px;
                           font-weight: 700; letter-spacing: .6px; }
      QListWidget, QComboBox, QLineEdit { background: #181a1f; color: #edf0f5;
        border: 1px solid #343841; border-radius: 8px; padding: 7px 9px; selection-background-color: #254f8f; }
      QListWidget { padding: 5px; outline: 0; }
      QListWidget::item { border-radius: 7px; padding: 9px 8px; margin: 1px 0; }
      QListWidget::item:selected { background: #284f87; color: white; }
      QListWidget::item:hover { background: #292c34; }
      QComboBox::drop-down { border: 0; width: 28px; }
      QComboBox QAbstractItemView { background: #202229; border: 1px solid #414650; padding: 4px; }
      QLineEdit:focus, QComboBox:focus, QListWidget:focus { border: 1px solid #4891ff; }
      QCheckBox { color: #daddE5; spacing: 9px; padding: 4px 0; }
      QCheckBox::indicator { width: 17px; height: 17px; border: 1px solid #535965;
                            background: #181a1f; border-radius: 5px; }
      QCheckBox::indicator:checked { background: #4891ff; border-color: #4891ff; }
      QPushButton { background: #30333b; color: #f5f7fa; border: 1px solid #414650;
                    border-radius: 9px; padding: 9px 14px; font-weight: 600; }
      QPushButton:hover { background: #3a3e48; }
      QPushButton:pressed { background: #252830; padding-top: 10px; padding-bottom: 8px; }
      QPushButton#primaryButton { background: #3f82e8; border-color: #5797f6; }
      QPushButton#primaryButton:hover { background: #4a8ff4; }
      QPushButton#primaryButton:pressed { background: #326fc8; }
      QSlider::groove:horizontal { height: 5px; background: #343841; border-radius: 2px; }
      QSlider::sub-page:horizontal { background: #4891ff; border-radius: 2px; }
      QSlider::handle:horizontal { width: 17px; margin: -7px 0; background: #eef3fb;
                                  border: 1px solid #7f8998; border-radius: 8px; }
      QSplitter::handle { background: transparent; width: 8px; }
      QSplitter::handle:hover { background: rgba(72,145,255,70); border-radius: 3px; }
      QScrollArea { border: 0; background: transparent; }
      QScrollBar:vertical { background: transparent; width: 10px; }
      QScrollBar::handle:vertical { background: #494e59; border-radius: 5px; min-height: 30px; }
      QToolTip { background: #2a2d34; color: white; border: 1px solid #4b515d; padding: 6px; }
    )";
  }

  void build_ui() {
    auto* root = new QWidget;
    root->setObjectName("root");
    auto* shell = new QVBoxLayout(root);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    shell->addWidget(build_header());

    auto* body = new QWidget;
    auto* body_layout = new QHBoxLayout(body);
    body_layout->setContentsMargins(14, 12, 14, 12);
    body_layout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(10);
    splitter->addWidget(build_left_dock());
    splitter->addWidget(build_program());
    splitter->addWidget(build_inspector());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({220, 720, 300});
    body_layout->addWidget(splitter);
    shell->addWidget(body, 1);
    shell->addWidget(build_status());
    setCentralWidget(root);
  }

  QWidget* build_header() {
    auto* bar = new QFrame;
    bar->setObjectName("topBar");
    bar->setFixedHeight(68);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 9, 20, 9);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel("SubliminalCam Studio");
    title->setObjectName("appTitle");
    auto* subtitle = new QLabel("Virtual camera control room");
    subtitle->setObjectName("subtitle");
    titles->addWidget(title);
    titles->addWidget(subtitle);
    layout->addLayout(titles);
    layout->addStretch();
    output_button_ = new QPushButton("Stop output");
    output_button_->setCheckable(true);
    output_button_->setChecked(true);
    output_button_->setAccessibleName("Start or stop virtual camera output");
    layout->addWidget(output_button_, 0, Qt::AlignVCenter);
    live_badge_ = new QLabel("●  OUTPUT LIVE");
    live_badge_->setObjectName("liveBadge");
    live_badge_->setAccessibleName("Virtual camera output is live");
    layout->addWidget(live_badge_, 0, Qt::AlignVCenter);
    connect(output_button_, &QPushButton::toggled, this, [this](bool running) {
      output_enabled_ = running;
      output_button_->setText(running ? "Stop output" : "Start output");
      live_badge_->setText(running ? "●  OUTPUT LIVE" : "○  OUTPUT PAUSED");
      live_badge_->setObjectName(running ? "liveBadge" : "pausedBadge");
      live_badge_->style()->unpolish(live_badge_);
      live_badge_->style()->polish(live_badge_);
      live_badge_->setAccessibleName(running ? "Virtual camera output is live"
                                             : "Virtual camera output is paused");
      if (!running) {
        std::scoped_lock lock(frame_writer_mutex_);
        frame_writer_.invalidate();
      }
      show_status(running ? "Virtual camera output started"
                          : "Virtual camera output paused");
    });
    return bar;
  }

  QWidget* build_left_dock() {
    auto* panel = new QFrame;
    panel->setObjectName("card");
    panel->setMinimumWidth(190);
    panel->setMaximumWidth(300);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 13, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(section_label("SCENES"));
    scenes_ = new QListWidget;
    scenes_->setMinimumHeight(92);
    scenes_->addItem("Main Camera");
    scenes_->setCurrentRow(0);
    layout->addWidget(scenes_);
    layout->addSpacing(8);
    layout->addWidget(section_label("SOURCES"));
    sources_ = new QListWidget;
    sources_->addItems({"▣  Video Capture Device", "◈  Prompt Overlay", "▧  Background"});
    sources_->setCurrentRow(0);
    layout->addWidget(sources_, 1);
    auto* hint = new QLabel("Drag the dividers to resize your workspace.");
    hint->setObjectName("subtitle");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return panel;
  }

  QWidget* build_program() {
    auto* area = new QWidget;
    area->setMinimumWidth(450);
    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(9);
    auto* heading = new QHBoxLayout;
    auto* program = new QLabel("PROGRAM");
    program->setObjectName("programLabel");
    metrics_ = new QLabel("OFFLINE");
    metrics_->setObjectName("metric");
    heading->addWidget(program);
    heading->addStretch();
    heading->addWidget(metrics_);
    layout->addLayout(heading);
    preview_ = new PreviewWidget;
    layout->addWidget(preview_, 1);
    return area;
  }

  QWidget* build_inspector() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(270);
    scroll->setMaximumWidth(380);
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* camera_content = new QWidget;
    auto* camera_layout = new QVBoxLayout(camera_content);
    camera_layout->setContentsMargins(0, 0, 0, 0);
    camera_layout->setSpacing(8);
    camera_layout->addWidget(section_label("CAMERA"));
    camera_combo_ = new QComboBox;
    camera_combo_->setAccessibleName("Physical camera");
    camera_layout->addWidget(camera_combo_);
    mirror_ = new QCheckBox("Mirror camera");
    camera_layout->addWidget(mirror_);
    layout->addWidget(card(camera_content));

    auto* background_content = new QWidget;
    auto* background_layout = new QVBoxLayout(background_content);
    background_layout->setContentsMargins(0, 0, 0, 0);
    background_layout->setSpacing(8);
    background_layout->addWidget(section_label("BACKGROUND"));
    background_combo_ = new QComboBox;
    background_combo_->addItems({"Camera", "Background blur", "Warm studio", "Gradient", "Image…"});
    background_layout->addWidget(background_combo_);
    blur_caption_ = new QLabel("Blur strength");
    blur_caption_->setObjectName("subtitle");
    background_layout->addWidget(blur_caption_);
    blur_slider_ = new QSlider(Qt::Horizontal);
    blur_slider_->setRange(0, 32);
    blur_slider_->setAccessibleName("Blur strength");
    background_layout->addWidget(blur_slider_);
    layout->addWidget(card(background_content));

    auto* prompt_content = new QWidget;
    auto* prompt_layout = new QVBoxLayout(prompt_content);
    prompt_layout->setContentsMargins(0, 0, 0, 0);
    prompt_layout->setSpacing(9);
    prompt_layout->addWidget(section_label("ON-SCREEN PROMPT"));
    prompt_edit_ = new QLineEdit;
    prompt_edit_->setPlaceholderText("Write a visible prompt…");
    prompt_edit_->setClearButtonEnabled(true);
    prompt_layout->addWidget(prompt_edit_);
    prompts_enabled_ = new QCheckBox("Enable disclosed prompts");
    prompt_layout->addWidget(prompts_enabled_);
    auto* actions = new QHBoxLayout;
    show_now_ = new QPushButton("Show now");
    save_ = new QPushButton("Save scene");
    save_->setObjectName("primaryButton");
    actions->addWidget(show_now_);
    actions->addWidget(save_);
    prompt_layout->addLayout(actions);
    auto* disclosure = new QLabel("The output badge remains visible whenever prompts are enabled.");
    disclosure->setObjectName("subtitle");
    disclosure->setWordWrap(true);
    prompt_layout->addWidget(disclosure);
    layout->addWidget(card(prompt_content));
    layout->addStretch();
    scroll->setWidget(panel);

    connect(camera_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
      if (controls_loaded_) start_camera();
    });
    connect(background_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (!controls_loaded_) return;
      if (index == static_cast<int>(BackgroundMode::image)) choose_background();
      sync_settings();
    });
    connect(blur_slider_, &QSlider::valueChanged, this, [this](int value) {
      blur_radius_ = value;
      settings_.blur_radius = value;
      blur_caption_->setText(QString("Blur strength  ·  %1").arg(value));
    });
    connect(mirror_, &QCheckBox::toggled, this, [this](bool checked) {
      mirror_enabled_ = checked;
      settings_.mirror = checked;
    });
    connect(prompts_enabled_, &QCheckBox::toggled, this, [this](bool) { sync_settings(); });
    connect(show_now_, &QPushButton::clicked, this, [this] { show_prompt_now(); });
    connect(save_, &QPushButton::clicked, this, [this] { save_scene(); });
    return scroll;
  }

  QWidget* build_status() {
    auto* bar = new QFrame;
    bar->setObjectName("statusBar");
    bar->setFixedHeight(36);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 0, 20, 0);
    status_ = new QLabel("Frames stay local · Virtual camera ready for video apps");
    status_->setObjectName("statusText");
    layout->addWidget(status_);
    layout->addStretch();
    auto* privacy = new QLabel("LOCAL ONLY");
    privacy->setObjectName("sectionLabel");
    layout->addWidget(privacy);
    return bar;
  }

  void load_controls() {
    int selected = 0;
    cameras_ = enumerate_cameras();
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
      camera_combo_->addItem(QString::fromStdWString(cameras_[i].name));
      if (cameras_[i].symbolic_link == settings_.camera_id) selected = static_cast<int>(i);
    }
    if (!cameras_.empty()) camera_combo_->setCurrentIndex(selected);
    background_combo_->setCurrentIndex(static_cast<int>(settings_.background_mode));
    blur_slider_->setValue(settings_.blur_radius);
    mirror_->setChecked(settings_.mirror);
    prompts_enabled_->setChecked(settings_.prompts.enabled);
    if (!settings_.prompts.messages.empty())
      prompt_edit_->setText(QString::fromStdWString(settings_.prompts.messages.front()));
    mirror_enabled_ = settings_.mirror;
    background_mode_ = static_cast<int>(settings_.background_mode);
    blur_radius_ = settings_.blur_radius;
    if (!settings_.image_path.empty()) {
      if (auto loaded = load_image(settings_.image_path, 1280, 720))
        custom_background_ = std::make_shared<Frame>(std::move(*loaded));
    }
    controls_loaded_ = true;
  }

  void sync_settings() {
    settings_.prompts.messages = {prompt_edit_->text().toStdWString()};
    settings_.prompts.enabled = prompts_enabled_->isChecked();
    settings_.background_mode = static_cast<BackgroundMode>(background_combo_->currentIndex());
    settings_.blur_radius = blur_slider_->value();
    settings_.mirror = mirror_->isChecked();
    settings_ = sanitize(std::move(settings_));
    background_mode_ = static_cast<int>(settings_.background_mode);
    blur_radius_ = settings_.blur_radius;
    mirror_enabled_ = settings_.mirror;
    scheduler_.configure(settings_.prompts, now_ms());
  }

  void start_camera() {
    if (!capture_ || camera_combo_->currentIndex() < 0 ||
        camera_combo_->currentIndex() >= static_cast<int>(cameras_.size())) return;
    settings_.camera_id = cameras_[static_cast<std::size_t>(camera_combo_->currentIndex())].symbolic_link;
    show_status("Connecting camera…");
    capture_->start(settings_.camera_id);
  }

  void choose_background() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose background", {}, "Images (*.png *.jpg *.jpeg);;All files (*.*)");
    if (path.isEmpty()) {
      background_combo_->setCurrentIndex(static_cast<int>(settings_.background_mode));
      return;
    }
    auto loaded = load_image(path.toStdWString(), 1280, 720);
    if (!loaded) {
      show_status("That image could not be decoded", true);
      return;
    }
    {
      std::scoped_lock lock(background_mutex_);
      custom_background_ = std::make_shared<Frame>(std::move(*loaded));
    }
    settings_.image_path = path.toStdWString();
    show_status("Background image loaded");
  }

  void show_prompt_now() {
    sync_settings();
    scheduler_.show_now(now_ms());
    {
      std::scoped_lock lock(prompt_mutex_);
      prompt_ = scheduler_.tick(now_ms());
    }
    show_status("Prompt sent to program output");
  }

  void save_scene() {
    sync_settings();
    show_status(save_settings(settings_, settings_path()) ? "Scene saved" : "Could not save settings",
                false);
  }

  void show_status(const QString& message, bool error = false) {
    status_->setText(message);
    status_->setStyleSheet(error ? "color:#ff737a" : "color:#f0f2f7");
    status_reset_at_ = now_ms() + 2600;
  }

  void tick() {
    const auto current = now_ms();
    const auto next = scheduler_.tick(current);
    {
      std::scoped_lock lock(prompt_mutex_);
      prompt_ = next;
    }
    if (current - last_stats_at_ >= 1000) {
      const auto frames = rendered_frames_.load();
      const auto elapsed = std::max<std::int64_t>(1, current - last_stats_at_);
      const int fps = static_cast<int>((frames - last_stats_frames_) * 1000 / elapsed);
      metrics_->setText(QString("%1 FPS   ·   1280 × 720   ·   %2 ms")
                            .arg(fps).arg(processing_us_.load() / 1000));
      last_stats_frames_ = frames;
      last_stats_at_ = current;
    }
    if (status_reset_at_ && current >= status_reset_at_) {
      status_reset_at_ = 0;
      status_->setStyleSheet({});
      status_->setText("Frames stay local · Virtual camera ready for video apps");
    }
  }

  void process_frame(std::shared_ptr<Frame> frame) {
    if (!frame) return;
    const auto started = std::chrono::steady_clock::now();
    if (mirror_enabled_) mirror_horizontal(*frame);
    std::shared_ptr<Frame> custom;
    {
      std::scoped_lock lock(background_mutex_);
      custom = custom_background_;
    }
    apply_effect(*frame, static_cast<BackgroundMode>(background_mode_.load()),
                 blur_radius_.load(), custom);
    PromptState prompt;
    {
      std::scoped_lock lock(prompt_mutex_);
      prompt = prompt_;
    }
    draw_prompt(*frame, prompt);
    if (output_enabled_) {
      std::scoped_lock lock(frame_writer_mutex_);
      frame_writer_.write(*frame, static_cast<std::uint64_t>(now_ms()) * 10000u);
    }
    if (!shutting_down_) preview_->submit(std::move(frame));
    processing_us_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    ++rendered_frames_;
  }

  static void apply_effect(Frame& frame, BackgroundMode mode, int radius,
                           const std::shared_ptr<Frame>& custom) {
    thread_local Frame blur_scratch;
    switch (mode) {
      case BackgroundMode::blur:
        box_blur_in_place(frame, radius, blur_scratch);
        break;
      case BackgroundMode::solid:
        std::fill(frame.pixels.begin(), frame.pixels.end(), Pixel{46, 35, 27, 255});
        break;
      case BackgroundMode::gradient:
        for (int y = 0; y < frame.height; ++y) {
          const auto a = frame.height == 1 ? 0u :
              static_cast<unsigned>((255ull * y) / (frame.height - 1));
          const Pixel color{mix_channel(24, 58, static_cast<std::uint8_t>(a)),
                            mix_channel(14, 40, static_cast<std::uint8_t>(a)),
                            mix_channel(10, 24, static_cast<std::uint8_t>(a)), 255};
          std::fill_n(frame.pixels.begin() + static_cast<std::size_t>(y) * frame.width,
                      frame.width, color);
        }
        break;
      case BackgroundMode::image:
        if (custom && custom->width == frame.width && custom->height == frame.height)
          frame.pixels = custom->pixels;
        break;
      case BackgroundMode::none:
      default:
        break;
    }
  }

  static void draw_prompt(Frame& frame, const PromptState& prompt) {
    if (!prompt.disclosure_visible && !prompt.prompt_visible) return;
    QImage image(reinterpret_cast<uchar*>(frame.pixels.data()), frame.width, frame.height,
                 frame.width * static_cast<int>(sizeof(Pixel)), QImage::Format_ARGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (prompt.disclosure_visible) {
      const QRectF badge(18, 18, 190, 38);
      painter.setPen(QPen(QColor("#326849"), 1));
      painter.setBrush(QColor(18, 24, 21, 242));
      painter.drawRoundedRect(badge, 10, 10);
      painter.setPen(QColor("#71ec9c"));
      painter.setFont(QFont("Segoe UI Variable Text", 9, QFont::DemiBold));
      painter.drawText(badge, Qt::AlignCenter, "PROMPTS ENABLED");
    }
    if (prompt.prompt_visible) {
      const QRectF box(frame.width / 8.0, frame.height / 2.0 - 58,
                       frame.width * 3.0 / 4.0, 116);
      painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
      painter.setBrush(QColor(247, 248, 250, 245));
      painter.drawRoundedRect(box, 18, 18);
      painter.setPen(QColor("#111720"));
      painter.setFont(QFont("Segoe UI Variable Display", 30, QFont::DemiBold));
      painter.drawText(box.adjusted(24, 10, -24, -10), Qt::AlignCenter,
                       QString::fromStdWString(prompt.message));
    }
  }

  AppSettings settings_;
  PromptScheduler scheduler_;
  PromptState prompt_;
  std::mutex prompt_mutex_;
  std::mutex background_mutex_;
  std::shared_ptr<Frame> custom_background_;
  SharedFrameWriter frame_writer_;
  std::mutex frame_writer_mutex_;
  std::unique_ptr<CameraCapture> capture_;
  std::vector<CameraDevice> cameras_;
  std::atomic_bool mirror_enabled_{true};
  std::atomic_int background_mode_{0};
  std::atomic_int blur_radius_{12};
  std::atomic_bool shutting_down_{false};
  std::atomic_bool output_enabled_{true};
  std::atomic_uint64_t rendered_frames_{0};
  std::atomic_uint64_t processing_us_{0};
  std::uint64_t last_stats_frames_{};
  std::int64_t last_stats_at_{now_ms()};
  std::int64_t status_reset_at_{};
  bool controls_loaded_{};
  QTimer tick_timer_;
  PreviewWidget* preview_{};
  QListWidget* scenes_{};
  QListWidget* sources_{};
  QComboBox* camera_combo_{};
  QComboBox* background_combo_{};
  QSlider* blur_slider_{};
  QLabel* blur_caption_{};
  QCheckBox* mirror_{};
  QLineEdit* prompt_edit_{};
  QCheckBox* prompts_enabled_{};
  QPushButton* show_now_{};
  QPushButton* save_{};
  QLabel* metrics_{};
  QLabel* status_{};
  QLabel* live_badge_{};
  QPushButton* output_button_{};
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("SubliminalCam Studio");
  QApplication::setOrganizationName("SubliminalCam");
  QApplication::setStyle("Fusion");

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    QMessageBox::critical(nullptr, "SubliminalCam",
                          "Windows COM initialization failed. The camera controller cannot start.");
    return 1;
  }
  const HRESULT media_foundation_result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(media_foundation_result)) {
    QMessageBox::critical(nullptr, "SubliminalCam",
                          "Windows Media Foundation initialization failed. Restart Windows and try again.");
    if (SUCCEEDED(com_result)) CoUninitialize();
    return 1;
  }

  int result = 0;
  {
    StudioWindow window;
    window.show();
    result = app.exec();
  }
  MFShutdown();
  if (SUCCEEDED(com_result)) CoUninitialize();
  return result;
}
