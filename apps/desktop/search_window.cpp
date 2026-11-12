#include "search_window.hpp"
#include "fileweaver/core/utc_time.hpp"
#include "preview_highlighter.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimeZone>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <utility>

namespace fileweaver::desktop {
namespace {
QString preview(const QString &text, qsizetype limit) {
    if (text.size() <= limit) {
        return text;
    }
    if (limit > 0 && text.at(limit - 1).isHighSurrogate()) {
        --limit;
    }
    return text.left(limit) + QStringLiteral("…");
}

QString parentText(const QString &path) {
    const auto slash = (std::max)(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    if (slash < 0) {
        return {};
    }
    return path.left(slash == 0 ? 1 : slash == 2 && path.at(1) == u':' ? 3 : slash);
}
} // namespace

SearchWindow::SearchWindow(SearchController::Search search, QWidget *parent, HotkeyApi hotkey_api,
                           FileOpenController::Open open_file, FileOpenController::Open reveal_file,
                           WatchApi watch_api, IndexApi index_api, QString managed_database)
    : QWidget(parent), managed_database_(managed_database.isEmpty() ? managedIndexDatabase()
                                                                    : std::move(managed_database)),
      hotkey_(std::move(hotkey_api)),
      controller_(search ? std::move(search)
                         : SearchController::Search([managed = managed_database_](
                                                        const auto &request, const auto &stop) {
                               return desktopSearchFiles(request, stop, managed);
                           })),
      opener_(std::move(open_file), nullptr, std::move(reveal_file)),
      watcher_(std::move(watch_api)), indexer_(std::move(index_api)) {
    auto initial_font = this->font();
    initial_font.setPointSize(10);
    setFont(initial_font);
    setWindowTitle(QStringLiteral("FileWeaver — 檔名搜尋"));
    QSize initial_size(1280, 900);
    if (const auto *display = screen()) {
        const auto available = display->availableGeometry().size();
        if (available.width() >= 800 && available.height() >= 540) {
            initial_size.setWidth(std::min(initial_size.width(), available.width() - 40));
            initial_size.setHeight(std::min(initial_size.height(), available.height() - 60));
        }
    }
    resize(initial_size);
    setMinimumSize(760, 480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(8);
    auto *heading = new QLabel(QStringLiteral("FileWeaver"), this);
    auto font = heading->font();
    font.setPointSize(22);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(new QLabel(QStringLiteral("加入搜尋資料夾，之後直接搜尋檔案"), this));
    auto *onboarding = new QHBoxLayout();
    add_folder_ = new QPushButton(QStringLiteral("加入搜尋資料夾…"), this);
    add_folder_->setObjectName(QStringLiteral("addFolder"));
    index_cancel_ = new QPushButton(QStringLiteral("取消建立"), this);
    index_cancel_->setObjectName(QStringLiteral("cancelIndex"));
    index_cancel_->setEnabled(false);
    index_progress_ = new QProgressBar(this);
    index_progress_->setObjectName(QStringLiteral("indexProgress"));
    index_progress_->setRange(0, 0);
    index_progress_->setTextVisible(false);
    index_progress_->hide();
    onboarding->addWidget(add_folder_);
    onboarding->addWidget(index_cancel_);
    onboarding->addWidget(index_progress_, 1);
    layout->addLayout(onboarding);
    index_status_ =
        new QLabel(QStringLiteral("選擇資料夾後由程式建立索引，不需準備資料庫。"), this);
    index_status_->setObjectName(QStringLiteral("indexStatus"));
    index_status_->setTextFormat(Qt::PlainText);
    index_status_->setWordWrap(true);
    index_status_->setMaximumHeight(96);
    layout->addWidget(index_status_);
    database_path_ = new QLineEdit(this);
    database_path_->setObjectName(QStringLiteral("databasePath"));
    database_path_->setAccessibleName(QStringLiteral("目前索引"));
    database_path_->setPlaceholderText(QStringLiteral("尚未選擇索引"));
    database_path_->setReadOnly(true);
    auto *choose = new QPushButton(QStringLiteral("使用既有索引…"), this);
    choose_database_ = choose;
    choose->setObjectName(QStringLiteral("chooseDatabase"));
    onboarding->addWidget(database_path_, 1);
    onboarding->addWidget(choose);
    auto search_row = std::make_unique<QHBoxLayout>();
    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("query"));
    query_->setAccessibleName(QStringLiteral("檔名搜尋"));
    query_->setPlaceholderText(QStringLiteral("輸入關鍵字搜尋；留空顯示檔案"));
    query_->setClearButtonEnabled(true);
    query_->setMaxLength(4096);
    query_->installEventFilter(this);
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("mode"));
    mode_->setAccessibleName(QStringLiteral("比對方式"));
    mode_->addItems({QStringLiteral("包含文字"), QStringLiteral("檔名開頭"),
                     QStringLiteral("完整檔名"), QStringLiteral("模糊檔名"),
                     QStringLiteral("Glob 檔名"), QStringLiteral("包含路徑"),
                     QStringLiteral("模糊路徑")});
    auto *fuzzy_settings = new QPushButton(QStringLiteral("模糊設定…"), this);
    fuzzy_settings->setObjectName(QStringLiteral("fuzzySettingsButton"));
    fuzzy_settings->setEnabled(false);
    connect(fuzzy_settings, &QPushButton::clicked, this, &SearchWindow::configureFuzzy);
    connect(mode_, &QComboBox::currentIndexChanged, this,
            [fuzzy_settings](int index) { fuzzy_settings->setEnabled(index == 3 || index == 6); });
    auto *filter_settings = new QPushButton(QStringLiteral("篩選／排序…"), this);
    filter_settings->setObjectName(QStringLiteral("filterSettingsButton"));
    connect(filter_settings, &QPushButton::clicked, this, &SearchWindow::configureFilters);
    auto *search_button = new QPushButton(QStringLiteral("搜尋"), this);
    search_button_ = search_button;
    search_button->setObjectName(QStringLiteral("search"));
    search_row->addWidget(query_, 1);
    search_row->addWidget(mode_);
    search_row->addWidget(fuzzy_settings);
    search_row->addWidget(filter_settings);
    search_row->addWidget(search_button);
    layout->addLayout(search_row.release());
    auto hotkey_row = std::make_unique<QHBoxLayout>();
    hotkey_enabled_ = new QCheckBox(QStringLiteral("啟用全域快捷鍵"), this);
    hotkey_enabled_->setObjectName(QStringLiteral("hotkeyEnabled"));
    hotkey_sequence_ = new QKeySequenceEdit(QKeySequence(QStringLiteral("Ctrl+Alt+Space")), this);
    hotkey_sequence_->setObjectName(QStringLiteral("hotkeySequence"));
    hotkey_sequence_->setAccessibleName(QStringLiteral("全域快捷鍵組合"));
    hotkey_sequence_->setMaximumWidth(210);
    hotkey_row->addWidget(hotkey_enabled_);
    hotkey_row->addWidget(hotkey_sequence_);
    hotkey_row->addStretch();
    auto *settings = new QWidget(this);
    settings->setObjectName(QStringLiteral("advancedSettings"));
    auto *settings_layout = new QVBoxLayout(settings);
    settings_layout->setContentsMargins(0, 0, 0, 0);
    auto *compact = new QPushButton(QStringLiteral("收起快捷鍵／監控／偏好設定"), this);
    compact->setObjectName(QStringLiteral("compactSettings"));
    compact->setCheckable(true);
    layout->addWidget(compact);
    layout->addWidget(settings);
    connect(compact, &QPushButton::toggled, this, [settings, compact](bool hidden) {
        settings->setVisible(!hidden);
        compact->setText(hidden ? QStringLiteral("展開快捷鍵／監控／偏好設定")
                                : QStringLiteral("收起快捷鍵／監控／偏好設定"));
    });
    settings_layout->addLayout(hotkey_row.release());
    hotkey_status_ =
        new QLabel(QStringLiteral("快捷鍵未啟用；可選擇記住組合，下次仍需手動啟用。"), this);
    hotkey_status_->setObjectName(QStringLiteral("hotkeyStatus"));
    hotkey_status_->setTextFormat(Qt::PlainText);
    hotkey_status_->setWordWrap(true);
    settings_layout->addWidget(hotkey_status_);
    connect(hotkey_enabled_, &QCheckBox::toggled, this, &SearchWindow::updateHotkey);
    connect(&hotkey_, &GlobalHotkey::activated, this, &SearchWindow::activateSearch);
    auto *preferences_row = new QHBoxLayout();
    remember_ = new QCheckBox(QStringLiteral("記住非敏感偏好設定"), this);
    remember_->setObjectName(QStringLiteral("rememberPreferences"));
    remember_->setEnabled(false);
    font_size_ = new QSpinBox(this);
    font_size_->setObjectName(QStringLiteral("interfaceFontSize"));
    font_size_->setAccessibleName(QStringLiteral("介面字級"));
    font_size_->setRange(8, 24);
    font_size_->setValue(10);
    preferences_row->addWidget(remember_);
    preferences_row->addWidget(new QLabel(QStringLiteral("字級"), this));
    preferences_row->addWidget(font_size_);
    preferences_row->addStretch();
    settings_layout->addLayout(preferences_row);
    preferences_status_ =
        new QLabel(QStringLiteral("偏好設定不包含查詢、路徑或內容；啟用狀態不保存。"), this);
    preferences_status_->setObjectName(QStringLiteral("preferencesStatus"));
    preferences_status_->setTextFormat(Qt::PlainText);
    preferences_status_->setWordWrap(true);
    settings_layout->addWidget(preferences_status_);
    connect(remember_, &QCheckBox::toggled, this, [this] {
        if (!applying_preferences_) {
            preferences_dirty_ = true;
            savePreferences();
        }
    });
    connect(font_size_, &QSpinBox::valueChanged, this, [this](int size) {
        auto next = this->font();
        next.setPointSize(size);
        setFont(next);
        if (preview_text_) {
            auto fixed = preview_text_->font();
            fixed.setPointSize(size);
            preview_text_->setFont(fixed);
        }
        preferenceChanged();
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { preferenceChanged(); });
    connect(hotkey_sequence_, &QKeySequenceEdit::keySequenceChanged, this,
            [this] { preferenceChanged(); });
    connect(&preferences_, &PreferencesController::completed, this,
            [this](const PreferencesReply &reply) {
                remember_->setEnabled(true);
                if (!reply.ok) {
                    preferences_status_->setText(QStringLiteral("偏好設定未套用／保存：") +
                                                 preview(reply.error, 512));
                    return;
                }
                if (reply.loaded) {
                    applying_preferences_ = true;
                    if (reply.value.enabled && !preferences_dirty_) {
                        fuzzy_ = reply.value.fuzzy;
                        sort_ = reply.value.sort < 0 ? std::optional<SearchSort>{}
                                                     : static_cast<SearchSort>(reply.value.sort);
                        descending_ = reply.value.descending;
                        result_limit_ = static_cast<std::size_t>(reply.value.limit);
                        if (visible_limit_) {
                            visible_limit_->setValue(reply.value.limit);
                        }
                        hotkey_sequence_->setKeySequence(QKeySequence::fromString(
                            reply.value.hotkey, QKeySequence::PortableText));
                        mode_->setCurrentIndex(reply.value.mode);
                        font_size_->setValue(reply.value.font);
                    }
                    remember_->setChecked(reply.value.enabled);
                    applying_preferences_ = false;
                }
                preferences_status_->setText(
                    reply.value.enabled
                        ? QStringLiteral("已保存非敏感偏好；快捷鍵、監控與預覽仍需每次手動啟用。")
                        : QStringLiteral("未保存偏好；啟用狀態與內容不會保存。"));
            });
    auto watch_row = std::make_unique<QHBoxLayout>();
    watch_load_ = new QPushButton(QStringLiteral("載入監控根目錄"), this);
    watch_load_->setObjectName(QStringLiteral("watchLoad"));
    watch_load_->setEnabled(false);
    watch_root_ = new QComboBox(this);
    watch_root_->setObjectName(QStringLiteral("watchRoot"));
    watch_root_->setAccessibleName(QStringLiteral("監控根目錄"));
    watch_root_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    watch_root_->setMinimumContentsLength(20);
    watch_start_ = new QPushButton(QStringLiteral("啟用監控"), this);
    watch_start_->setObjectName(QStringLiteral("watchStart"));
    watch_stop_ = new QPushButton(QStringLiteral("停止監控"), this);
    watch_stop_->setObjectName(QStringLiteral("watchStop"));
    watch_row->addWidget(watch_load_);
    watch_row->addWidget(watch_root_, 1);
    watch_row->addWidget(watch_start_);
    watch_row->addWidget(watch_stop_);
    settings_layout->addLayout(watch_row.release());
    watch_status_ = new QLabel(QStringLiteral("監控未啟用；只有手動啟用後才會背景重掃。"), this);
    watch_status_->setObjectName(QStringLiteral("watchStatus"));
    watch_status_->setTextFormat(Qt::PlainText);
    watch_status_->setWordWrap(true);
    settings_layout->addWidget(watch_status_);
    connect(watch_load_, &QPushButton::clicked, this, [this] {
        if (watcher_.load(std::filesystem::path(database_.toStdWString()))) {
            watch_status_->setText(QStringLiteral("正在讀取已登錄根目錄…"));
            updateWatchControls();
        }
    });
    connect(watch_root_, &QComboBox::currentIndexChanged, this, [this] { updateWatchControls(); });
    connect(watch_start_, &QPushButton::clicked, this, [this] {
        const auto index = watch_root_->currentIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= watch_roots_.size()) {
            return;
        }
        if (watcher_.start({std::filesystem::path(database_.toStdWString()),
                            watch_roots_[static_cast<std::size_t>(index)]})) {
            watch_scans_ = 0;
            watch_last_sync_.reset();
            watch_status_->setText(QStringLiteral("正在啟用監控；將先核對選定根目錄。"));
            updateWatchControls();
        }
    });
    connect(watch_stop_, &QPushButton::clicked, this, [this] {
        watcher_.cancel();
        watch_status_->setText(QStringLiteral("已要求停止，正在取消背景工作…"));
        updateWatchControls();
    });
    connect(&watcher_, &WatchController::updated, this, &SearchWindow::showWatchReply);
    updateWatchControls();
    status_ = new QLabel(QStringLiteral("加入搜尋資料夾，或使用既有索引即可開始。"), this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setMaximumHeight(64);
    layout->addWidget(status_);
    results_ = new QTreeView(this);
    results_->setObjectName(QStringLiteral("results"));
    results_->setAccessibleName(QStringLiteral("搜尋結果"));
    results_->setRootIsDecorated(false);
    results_->setAlternatingRowColors(true);
    results_->setUniformRowHeights(true);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    model_ = new QStandardItemModel(results_);
    model_->setHorizontalHeaderLabels(
        {QStringLiteral("檔名"), QStringLiteral("所在位置"), QStringLiteral("大小")});
    results_->setModel(model_);
    results_->installEventFilter(this);
    results_->setColumnWidth(0, 285);
    results_->setColumnWidth(1, 460);
    results_->header()->setStretchLastSection(true);
    auto *preview_row = new QHBoxLayout();
    preview_enabled_ = new QCheckBox(QStringLiteral("啟用內容預覽（讀取選取檔案）"), this);
    preview_enabled_->setObjectName(QStringLiteral("previewEnabled"));
    auto *clear_preview = new QPushButton(QStringLiteral("清除預覽快取"), this);
    clear_preview->setObjectName(QStringLiteral("previewClear"));
    auto *external_open = new QPushButton(QStringLiteral("以預設程式開啟…"), this);
    external_open->setObjectName(QStringLiteral("previewOpen"));
    preview_row->addWidget(preview_enabled_);
    preview_row->addWidget(clear_preview);
    preview_row->addWidget(external_open);
    preview_row->addStretch();
    layout->addLayout(preview_row);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(results_);
    auto *sidebar = new QWidget(splitter);
    auto *sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    auto *types = new QGroupBox(QStringLiteral("檔案類型（可複選；未勾選為全部）"), sidebar);
    types->setObjectName(QStringLiteral("typeFilters"));
    types->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto types_layout = std::make_unique<QHBoxLayout>();
    const std::vector<std::pair<QString, std::vector<std::string>>> groups{
        {QStringLiteral("圖片"),
         {"png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff", "svg", "ico"}},
        {QStringLiteral("文件"),
         {"pdf", "txt", "md", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "csv"}},
        {QStringLiteral("影音"),
         {"mp4", "mkv", "avi", "mov", "webm", "mp3", "wav", "flac", "m4a", "ogg"}},
        {QStringLiteral("程式碼"),
         {"cpp", "hpp", "c", "h", "py", "js", "ts", "json", "xml", "html", "css", "rs", "go",
          "java"}},
        {QStringLiteral("壓縮檔"), {"zip", "rar", "7z", "tar", "gz"}}};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        auto *check = new QCheckBox(groups[i].first, types);
        check->setObjectName(QStringLiteral("typeFilter%1").arg(i));
        QStringList suffixes;
        for (const auto &suffix : groups[i].second) {
            suffixes.append(QString::fromStdString(suffix));
        }
        check->setToolTip(suffixes.join(QStringLiteral(", ")));
        type_filters_.emplace_back(check, groups[i].second);
        types_layout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &SearchWindow::schedule);
    }
    auto *limit_row = new QHBoxLayout();
    limit_row->addWidget(new QLabel(QStringLiteral("最多顯示"), types));
    visible_limit_ = new QSpinBox(types);
    visible_limit_->setObjectName(QStringLiteral("visibleResultLimit"));
    visible_limit_->setRange(1, 1000);
    visible_limit_->setValue(static_cast<int>(result_limit_));
    limit_row->addWidget(visible_limit_);
    limit_row->addStretch();
    types_layout->addLayout(limit_row);
    types->setLayout(types_layout.release());
    layout->addWidget(types);
    connect(visible_limit_, &QSpinBox::valueChanged, this, [this](int value) {
        result_limit_ = static_cast<std::size_t>(value);
        preferenceChanged();
        schedule();
    });
    preview_pane_ = new QWidget(sidebar);
    preview_pane_->setObjectName(QStringLiteral("previewPane"));
    auto *preview_layout = new QVBoxLayout(preview_pane_);
    preview_status_ = new QLabel(QStringLiteral("預覽未啟用；不會讀取檔案內容。"), preview_pane_);
    preview_status_->setObjectName(QStringLiteral("previewStatus"));
    preview_status_->setTextFormat(Qt::PlainText);
    preview_status_->setWordWrap(true);
    preview_status_->setMaximumHeight(96);
    preview_image_ = new QLabel(preview_pane_);
    preview_image_->setObjectName(QStringLiteral("previewImage"));
    preview_image_->setAlignment(Qt::AlignCenter);
    preview_image_->setMinimumSize(1, 1);
    preview_image_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    preview_image_->installEventFilter(this);
    preview_text_ = new QPlainTextEdit(preview_pane_);
    preview_text_->setObjectName(QStringLiteral("previewText"));
    preview_text_->setAccessibleName(QStringLiteral("唯讀內容預覽"));
    preview_text_->setReadOnly(true);
    preview_text_->setUndoRedoEnabled(false);
    preview_text_->setMaximumBlockCount(2001);
    preview_text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto preview_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    preview_font.setPointSize(font_size_->value());
    preview_text_->setFont(preview_font);
    new PreviewHighlighter(preview_text_->document());
    preview_layout->addWidget(preview_status_);
    preview_layout->addWidget(preview_image_, 1);
    preview_image_->hide();
    preview_text_->hide();
    preview_layout->addWidget(preview_text_, 1);
    sidebar_layout->addWidget(preview_pane_, 1);
    sidebar_layout->addStretch(0);
    splitter->addWidget(sidebar);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    preview_pane_->hide();
    sidebar->hide();
    layout->addWidget(splitter, 1);
    connect(preview_enabled_, &QCheckBox::toggled, this, [this, sidebar](bool enabled) {
        sidebar->setVisible(enabled);
        preview_pane_->setVisible(enabled);
        previewer_.clear();
        preview_text_->clear();
        preview_image_->clear();
        preview_pixmap_ = {};
        preview_text_->hide();
        preview_image_->hide();
        requestPreview();
    });
    connect(clear_preview, &QPushButton::clicked, this, [this] {
        previewer_.clear();
        preview_text_->clear();
        preview_image_->clear();
        preview_pixmap_ = {};
        preview_text_->hide();
        preview_image_->hide();
        preview_status_->setText(QStringLiteral("快取已要求清除；重新選取檔案可再次預覽。"));
        preview_status_->show();
    });
    connect(external_open, &QPushButton::clicked, this,
            [this] { requestSelected(FileAction::open); });
    connect(&previewer_, &PreviewController::completed, this, [this](const PreviewReply &reply) {
        if (!preview_enabled_->isChecked()) {
            return;
        }
        preview_text_->clear();
        preview_image_->clear();
        preview_pixmap_ = {};
        preview_text_->hide();
        preview_image_->hide();
        preview_status_->setText(preview(reply.message, 512));
        preview_status_->setVisible(!reply.message.isEmpty());
        if (reply.ok) {
            preview_text_->setPlainText(reply.text);
            preview_text_->setVisible(reply.image.isNull());
            preview_image_->setVisible(!reply.image.isNull());
            if (!reply.image.isNull()) {
                preview_pixmap_ = QPixmap::fromImage(reply.image);
                updatePreviewImage();
            }
        }
    });
    auto selection_row = std::make_unique<QHBoxLayout>();
    selected_path_ = new QLineEdit(this);
    selected_path_->setObjectName(QStringLiteral("selectedPath"));
    selected_path_->setAccessibleName(QStringLiteral("選取的路徑預覽"));
    selected_path_->setReadOnly(true);
    selected_path_->setPlaceholderText(QStringLiteral("選取結果以查看路徑"));
    copy_ = new QPushButton(QStringLiteral("複製完整路徑"), this);
    copy_->setObjectName(QStringLiteral("copyPath"));
    copy_->setEnabled(false);
    open_ = new QPushButton(QStringLiteral("開啟檔案…"), this);
    open_->setObjectName(QStringLiteral("openFile"));
    open_->setEnabled(false);
    selection_row->addWidget(selected_path_, 1);
    selection_row->addWidget(open_);
    reveal_ = new QPushButton(QStringLiteral("顯示所在位置…"), this);
    reveal_->setObjectName(QStringLiteral("revealFile"));
    reveal_->setEnabled(false);
    selection_row->addWidget(reveal_);
    selection_row->addWidget(copy_);
    layout->addLayout(selection_row.release());
    layout->addWidget(
        new QLabel(QStringLiteral("↓ 進入結果 · ↑ ↓ 選取 · Enter 開啟確認 · "
                                  "Ctrl+Enter 顯示位置 · Ctrl+C 複製路徑 · Esc 隱藏／關閉"),
                   this));
    auto *note = new QLabel(
        QStringLiteral("結果來自索引；檔案變更後請更新索引。上限可在篩選／排序調整。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    debounce_.setSingleShot(true);
    debounce_.setInterval(180);
    connect(&debounce_, &QTimer::timeout, this, &SearchWindow::searchNow);
    connect(query_, &QLineEdit::textChanged, this, &SearchWindow::schedule);
    connect(mode_, &QComboBox::currentIndexChanged, this, &SearchWindow::schedule);
    connect(query_, &QLineEdit::returnPressed, this, &SearchWindow::searchNow);
    connect(search_button, &QPushButton::clicked, this, &SearchWindow::searchNow);
    connect(copy_, &QPushButton::clicked, this, &SearchWindow::copyPath);
    connect(open_, &QPushButton::clicked, this, [this] { requestSelected(FileAction::open); });
    connect(reveal_, &QPushButton::clicked, this, [this] { requestSelected(FileAction::reveal); });
    connect(results_, &QTreeView::doubleClicked, this,
            [this] { requestSelected(FileAction::open); });
    connect(&opener_, &FileOpenController::completed, this, [this](const OpenReply &reply) {
        status_->setText(QStringLiteral("上次檔案操作：") + preview(reply.message, 512));
        selectResult();
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getOpenFileName(
            this, QStringLiteral("選擇 FileWeaver 索引"), QString{},
            QStringLiteral("FileWeaver 索引 (*.sqlite *.db);;所有檔案 (*)"));
        if (!selected.isEmpty()) {
            setDatabase(selected);
        }
    });
    connect(add_folder_, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getExistingDirectory(
            this, QStringLiteral("選擇要加入搜尋的資料夾"), {},
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!selected.isEmpty()) {
            requestIndexFolder(selected);
        }
    });
    connect(index_cancel_, &QPushButton::clicked, this, [this] {
        indexer_.cancel();
        index_cancel_->setEnabled(false);
        index_status_->setText(QStringLiteral("正在取消；最後有效的索引會保留，請稍候…"));
    });
    connect(&indexer_, &IndexController::completed, this, &SearchWindow::showIndexReply);
    connect(results_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { selectResult(); });
    connect(&controller_, &SearchController::completed, this, &SearchWindow::showReply);
    auto *copy_shortcut = new QShortcut(QKeySequence::Copy, results_);
    copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy_shortcut, &QShortcut::activated, this, &SearchWindow::copyPath);
    auto *close_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(close_shortcut, &QShortcut::activated, this, [this] {
        if (hotkey_.isEnabled()) {
            preview_enabled_->setChecked(false);
            previewer_.clear();
            hide();
        } else {
            close();
        }
    });
    query_->setFocus();
}

void SearchWindow::updateHotkey(bool enabled) {
    const auto error =
        enabled ? hotkey_.enable(hotkey_sequence_->keySequence()) : hotkey_.disable();
    const QSignalBlocker blocker(hotkey_enabled_);
    hotkey_enabled_->setChecked(hotkey_.isEnabled());
    hotkey_sequence_->setEnabled(!hotkey_.isEnabled());
    hotkey_status_->setText(
        !error.isEmpty() ? error
        : hotkey_.isEnabled()
            ? QStringLiteral("已啟用 %1，按下可切回搜尋欄；修改組合前請先停用。")
                  .arg(hotkey_sequence_->keySequence().toString(QKeySequence::NativeText))
            : QStringLiteral("快捷鍵未啟用；可選擇記住組合，下次仍需手動啟用。"));
}

void SearchWindow::activateSearch() {
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    query_->setFocus(Qt::ShortcutFocusReason);
    query_->selectAll();
}

void SearchWindow::loadPreferences(const QString &path) {
    preferences_path_ = path;
    preferences_dirty_ = false;
    remember_->setEnabled(false);
    preferences_.load(path);
}
void SearchWindow::preferenceChanged() {
    if (applying_preferences_ || preferences_path_.isEmpty()) {
        return;
    }
    preferences_dirty_ = true;
    if (remember_->isChecked()) {
        savePreferences();
    }
}
void SearchWindow::savePreferences() {
    if (preferences_path_.isEmpty()) {
        return;
    }
    Preferences value;
    value.enabled = remember_->isChecked();
    value.fuzzy = fuzzy_;
    value.hotkey = hotkey_sequence_->keySequence().toString(QKeySequence::PortableText);
    value.font = font_size_->value();
    value.mode = mode_->currentIndex();
    value.sort = sort_ ? static_cast<int>(*sort_) : -1;
    value.limit = static_cast<int>(result_limit_);
    value.descending = descending_;
    preferences_status_->setText(QStringLiteral("正在保存非敏感偏好…"));
    preferences_.save(preferences_path_, std::move(value));
}
void SearchWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange && isMinimized()) {
        preview_enabled_->setChecked(false);
        previewer_.clear();
    }
    QWidget::changeEvent(event);
}

void SearchWindow::setDatabase(const QString &path) {
    if (indexer_.isBusy()) {
        return;
    }
    watcher_.cancel();
    previewer_.clear();
    preview_enabled_->setChecked(false);
    watch_roots_.clear();
    watch_root_->clear();
    watch_scans_ = 0;
    watch_last_sync_.reset();
    watch_status_->setText(QStringLiteral("索引已切換；監控預設關閉。"));
    database_ = path;
    updateWatchControls();
    database_path_->setText(path == managed_database_ ? QStringLiteral("程式管理的搜尋索引")
                                                      : QDir::toNativeSeparators(path));
    database_path_->setToolTip(QDir::toNativeSeparators(path));
    schedule();
    query_->setFocus();
}

void SearchWindow::requestIndexFolder(const QString &path) {
    if (path.isEmpty() || managed_database_.isEmpty() || indexer_.isBusy() || watcher_.isBusy() ||
        !index_confirmation_.isNull() || !confirmation_.isNull()) {
        index_status_->setText(QStringLiteral("請先完成目前工作／停止監控，再加入資料夾。"));
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("indexConfirmation"));
    dialog->setWindowTitle(QStringLiteral("加入搜尋資料夾"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(620, 280);
    auto *layout = new QVBoxLayout(dialog);
    auto *note = new QLabel(QStringLiteral("將建立下列資料夾的檔名、路徑、大小與時間索引。只處理你"
                                           "選的資料夾，不讀取檔案內容或修改來源。"),
                            dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *source = new QPlainTextEdit(dialog);
    source->setObjectName(QStringLiteral("indexRoot"));
    source->setReadOnly(true);
    source->setPlainText(QDir::toNativeSeparators(path));
    layout->addWidget(source);
    auto *storage = new QLabel(QStringLiteral("索引由程式保存於本機應用程式資料夾；不搬移或覆寫你既"
                                              "有的其他索引。監控與預覽需另外啟用。"),
                               dialog);
    storage->setWordWrap(true);
    storage->setToolTip(QDir::toNativeSeparators(managed_database_));
    layout->addWidget(storage);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->setObjectName(QStringLiteral("indexConfirmationButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("開始建立"));
    buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, path] {
        if (!watcher_.isBusy() &&
            indexer_.submit({std::filesystem::path(managed_database_.toStdWString()),
                             std::filesystem::path(path.toStdWString()),
                             {}})) {
            index_status_->setText(QStringLiteral("正在準備／掃描選定資料夾…可隨時要求取消。"));
            updateWatchControls();
            schedule();
        } else {
            index_status_->setText(QStringLiteral("尚有其他索引工作或監控，請稍後重試。"));
        }
    });
    connect(dialog, &QDialog::finished, this, [this] { index_confirmation_.clear(); });
    index_confirmation_ = dialog;
    dialog->open();
    buttons->button(QDialogButtonBox::Cancel)->setFocus();
}

void SearchWindow::showIndexReply(const IndexReply &reply) {
    updateWatchControls();
    if (reply.cancelled) {
        index_status_->setText(
            QStringLiteral("建立已取消；最後有效索引保留。新建的空索引可供下次重試。"));
        schedule();
        return;
    }
    if (!reply.ok) {
        index_status_->setText(QStringLiteral("建立索引失敗：") + preview(reply.message, 768));
        schedule();
        return;
    }
    const auto &summary = reply.summary;
    index_status_->setText(
        QStringLiteral("索引已更新：新增 %1／更新 %2，掃描 %3，略過 %4／錯誤 %5。%6")
            .arg(summary.added)
            .arg(summary.updated)
            .arg(summary.scan.files)
            .arg(summary.scan.skipped)
            .arg(summary.scan.errors)
            .arg(summary.missing_reconciled ? QStringLiteral("已核對消失檔案。")
                                            : QStringLiteral("Missing 尚未核對。")));
    filters_ = {};
    query_->clear();
    setDatabase(reply.database);
}

void SearchWindow::updateWatchControls() {
    const auto busy = watcher_.isBusy();
    const auto indexing = indexer_.isBusy();
    query_->setEnabled(!indexing);
    mode_->setEnabled(!indexing);
    search_button_->setEnabled(!indexing);
    add_folder_->setEnabled(!busy && !indexing && !managed_database_.isEmpty());
    choose_database_->setEnabled(!indexing);
    index_cancel_->setEnabled(indexing);
    index_progress_->setVisible(indexing);
    watch_load_->setEnabled(!database_.isEmpty() && !busy && !indexing);
    watch_root_->setEnabled(!busy && !indexing && !watch_roots_.empty());
    const auto index = watch_root_->currentIndex();
    const bool known = index >= 0 && static_cast<std::size_t>(index) < watch_roots_.size() &&
                       watch_roots_[static_cast<std::size_t>(index)].policy_known;
    watch_start_->setEnabled(!busy && !indexing && known);
    watch_stop_->setEnabled(busy);
}
void SearchWindow::showWatchReply(const WatchReply &reply) {
    if (reply.kind == WatchReplyKind::roots) {
        watch_roots_ = reply.roots;
        watch_last_sync_.reset();
        watch_root_->clear();
        for (const auto &root : watch_roots_) {
            const auto path =
                QString::fromUtf8(root.path.data(), static_cast<qsizetype>(root.path.size()));
            watch_root_->addItem(path + (root.policy_known
                                             ? QString{}
                                             : QStringLiteral("（需先重新索引保存設定）")));
            watch_root_->setItemData(watch_root_->count() - 1, path, Qt::ToolTipRole);
        }
    }
    auto message = reply.message;
    if (reply.last_sync) {
        watch_last_sync_ = reply.last_sync;
    }
    if (watch_last_sync_) {
        message += QStringLiteral(" 最後成功同步：") +
                   QDateTime::fromMSecsSinceEpoch(*watch_last_sync_ / 1000000)
                       .toUTC()
                       .toString(Qt::ISODate);
    }
    watch_status_->setText(message);
    updateWatchControls();
    if (reply.scans > watch_scans_) {
        watch_scans_ = reply.scans;
        searchNow();
    }
}

void SearchWindow::clearResults() {
    previewer_.clear();
    preview_text_->clear();
    preview_image_->clear();
    preview_pixmap_ = {};
    model_->removeRows(0, model_->rowCount());
    selected_path_->clear();
    copy_->setEnabled(false);
    open_->setEnabled(false);
    reveal_->setEnabled(false);
}

void SearchWindow::schedule() {
    controller_.cancel();
    debounce_.stop();
    clearResults();
    if (indexer_.isBusy()) {
        status_->setText(
            QStringLiteral("索引建立中；完成後會自動顯示檔案，可按「取消建立」停止。"));
        return;
    }
    if (database_.isEmpty()) {
        status_->setText(QStringLiteral("請加入搜尋資料夾，或使用既有索引。"));
    } else {
        status_->setText(query_->text().isEmpty() ? QStringLiteral("正在讀取檔案清單…")
                                                  : QStringLiteral("正在準備搜尋…"));
        debounce_.start();
    }
}

void SearchWindow::searchNow() {
    debounce_.stop();
    controller_.cancel();
    clearResults();
    if (database_.isEmpty() || indexer_.isBusy()) {
        schedule();
        return;
    }
    const auto query = query_->text().toUtf8();
    if (query.size() > 4096) {
        status_->setText(QStringLiteral("搜尋文字太長，請縮短後重試。"));
        return;
    }
    const bool browsing = query.isEmpty();
    SearchRequest request{std::filesystem::path(database_.toStdWString()),
                          browsing ? std::string("*") : query.toStdString()};
    const auto mode = mode_->currentIndex();
    request.mode = mode == 1                ? SearchMode::prefix
                   : mode == 2              ? SearchMode::exact
                   : mode == 3 || mode == 6 ? SearchMode::fuzzy
                   : mode == 4              ? SearchMode::glob
                   : mode == 5              ? SearchMode::path
                                            : SearchMode::substring;
    request.filters = filters_;
    request.limit = result_limit_;
    request.descending = descending_;
    request.sort = sort_.value_or(request.mode == SearchMode::fuzzy ? SearchSort::relevance
                                                                    : SearchSort::name);
    for (const auto &[check, extensions] : type_filters_) {
        if (check->isChecked()) {
            request.filters.extensions.insert(request.filters.extensions.end(), extensions.begin(),
                                              extensions.end());
        }
    }
    request.fuzzy = fuzzy_;
    request.fuzzy.path_components = mode == 6;
    if (browsing) {
        request.mode = SearchMode::glob;
        request.fuzzy.path_components = false;
        request.sort = sort_ && *sort_ != SearchSort::relevance ? *sort_ : SearchSort::name;
    }
    status_->setText(QStringLiteral("正在搜尋…"));
    controller_.submit(std::move(request));
}

void SearchWindow::configureFilters() {
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("filterSettings"));
    dialog->setWindowTitle(QStringLiteral("搜尋篩選與排序（本次執行）"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    auto *layout = new QFormLayout(dialog);
    const std::array<QString, 6> labels{
        QStringLiteral("最小位元組（含）"), QStringLiteral("最大位元組（含）"),
        QStringLiteral("精確根目錄路徑"),   QStringLiteral("修改時間晚於 UTC"),
        QStringLiteral("修改時間早於 UTC"), QStringLiteral("副檔名")};
    std::array<QLineEdit *, 6> fields{};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        fields[i] = new QLineEdit(dialog);
        fields[i]->setObjectName(QStringLiteral("filterValue%1").arg(i));
        fields[i]->setMaxLength(4096);
        layout->addRow(labels[i], fields[i]);
    }
    if (filters_.minimum_size) {
        fields[0]->setText(QString::number(*filters_.minimum_size));
    }
    if (filters_.maximum_size) {
        fields[1]->setText(QString::number(*filters_.maximum_size));
    }
    if (filters_.root) {
        fields[2]->setText(QString::fromStdWString(filters_.root->native()));
    }
    const auto timeText = [](std::optional<std::int64_t> value) {
        return value ? QDateTime::fromMSecsSinceEpoch(*value / 1000000)
                           .toUTC()
                           .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"))
                     : QString{};
    };
    fields[3]->setText(timeText(filters_.modified_after));
    fields[4]->setText(timeText(filters_.modified_before));
    fields[3]->setPlaceholderText(QStringLiteral("YYYY-MM-DDTHH:MM:SSZ"));
    fields[4]->setPlaceholderText(QStringLiteral("YYYY-MM-DDTHH:MM:SSZ"));
    auto *extension_enabled =
        new QCheckBox(QStringLiteral("使用副檔名條件（空值表示無副檔名）"), dialog);
    extension_enabled->setObjectName(QStringLiteral("filterExtensionEnabled"));
    extension_enabled->setChecked(filters_.extension.has_value());
    if (filters_.extension) {
        fields[5]->setText(QString::fromStdString(*filters_.extension));
    }
    layout->addRow(extension_enabled);
    auto *sort = new QComboBox(dialog);
    sort->setObjectName(QStringLiteral("filterSort"));
    sort->addItems({QStringLiteral("自動（依比對方式）"), QStringLiteral("檔名"),
                    QStringLiteral("大小"), QStringLiteral("修改時間"),
                    QStringLiteral("相關度（僅模糊）")});
    sort->setCurrentIndex(sort_ ? static_cast<int>(*sort_) + 1 : 0);
    layout->addRow(QStringLiteral("排序"), sort);
    auto *descending = new QCheckBox(QStringLiteral("主排序遞減（模糊模式不適用）"), dialog);
    descending->setObjectName(QStringLiteral("filterDescending"));
    descending->setChecked(descending_);
    layout->addRow(descending);
    auto *limit = new QSpinBox(dialog);
    limit->setObjectName(QStringLiteral("filterLimit"));
    limit->setRange(1, 1000);
    limit->setValue(static_cast<int>(result_limit_));
    layout->addRow(QStringLiteral("結果上限"), limit);
    auto *error = new QLabel(dialog);
    error->setObjectName(QStringLiteral("filterError"));
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addRow(error);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, dialog,
            [fields, extension_enabled, sort, descending, limit] {
                for (auto *field : fields) {
                    field->clear();
                }
                extension_enabled->setChecked(false);
                sort->setCurrentIndex(0);
                descending->setChecked(false);
                limit->setValue(100);
            });
    connect(buttons, &QDialogButtonBox::accepted, dialog,
            [this, dialog, fields, extension_enabled, sort, descending, limit, error] {
                try {
                    SearchFilters next;
                    const auto number = [](const QString &text) -> std::optional<std::int64_t> {
                        if (text.isEmpty()) {
                            return std::nullopt;
                        }
                        bool ok = false;
                        const auto value = text.toLongLong(&ok);
                        if (!ok || value < 0) {
                            throw std::runtime_error("大小必須是非負整數位元組。");
                        }
                        return value;
                    };
                    next.minimum_size = number(fields[0]->text());
                    next.maximum_size = number(fields[1]->text());
                    if (!fields[2]->text().isEmpty()) {
                        next.root = std::filesystem::path(fields[2]->text().toStdWString());
                    }
                    if (!fields[3]->text().isEmpty()) {
                        next.modified_after = parse_utc_time(fields[3]->text().toStdString());
                    }
                    if (!fields[4]->text().isEmpty()) {
                        next.modified_before = parse_utc_time(fields[4]->text().toStdString());
                    }
                    if (extension_enabled->isChecked()) {
                        next.extension = fields[5]->text().toStdString();
                    }
                    filters_ = std::move(next);
                    sort_ = sort->currentIndex() == 0
                                ? std::optional<SearchSort>{}
                                : static_cast<SearchSort>(sort->currentIndex() - 1);
                    descending_ = descending->isChecked();
                    result_limit_ = static_cast<std::size_t>(limit->value());
                    visible_limit_->setValue(limit->value());
                    dialog->accept();
                    preferenceChanged();
                    schedule();
                } catch (const std::exception &failure) {
                    error->setText(QString::fromUtf8(failure.what()).left(512));
                }
            });
    dialog->open();
}

void SearchWindow::configureFuzzy() {
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("fuzzySettings"));
    dialog->setWindowTitle(QStringLiteral("模糊搜尋設定（僅本次執行）"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    auto *layout = new QFormLayout(dialog);
    const std::array<QString, 7> labels{
        QStringLiteral("完整檔名加分"),      QStringLiteral("檔名開頭加分"),
        QStringLiteral("詞片段分數"),        QStringLiteral("子序列分數"),
        QStringLiteral("拼字容錯分數"),      QStringLiteral("候選上限"),
        QStringLiteral("父目錄命中分數上限")};
    const std::array<int, 7> values{fuzzy_.weights.exact, fuzzy_.weights.prefix,
                                    fuzzy_.weights.token, fuzzy_.weights.subsequence,
                                    fuzzy_.weights.typo,  static_cast<int>(fuzzy_.candidate_limit),
                                    fuzzy_.weights.path};
    std::array<QSpinBox *, 7> fields{};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        fields[i] = new QSpinBox(dialog);
        fields[i]->setObjectName(QStringLiteral("fuzzyValue%1").arg(i));
        fields[i]->setRange(i == 5 ? 1 : 0, 10000);
        fields[i]->setValue(values[i]);
        layout->addRow(labels[i], fields[i]);
    }
    auto *note = new QLabel(QStringLiteral("分數越高越優先；容錯分數會扣除編輯次數。超過候選上限會"
                                           "回報錯誤，請縮小查詢。設定不會保存。"),
                            dialog);
    note->setWordWrap(true);
    layout->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, fields] {
        fuzzy_.weights = {fields[0]->value(), fields[1]->value(), fields[2]->value(),
                          fields[3]->value(), fields[4]->value(), fields[6]->value()};
        fuzzy_.candidate_limit = static_cast<std::size_t>(fields[5]->value());
        preferenceChanged();
        schedule();
    });
    dialog->open();
}

void SearchWindow::showReply(const SearchReply &reply) {
    clearResults();
    if (!reply.error.isEmpty()) {
        status_->setText(QStringLiteral("無法搜尋：") + preview(reply.error, 512));
        return;
    }
    for (const auto &result : reply.results) {
        const auto path = QDir::toNativeSeparators(QString::fromStdString(result.display_path));
        auto name =
            std::make_unique<QStandardItem>(preview(QString::fromStdString(result.name), 256));
        auto location = std::make_unique<QStandardItem>(preview(parentText(path), 512));
        auto size = std::make_unique<QStandardItem>(QLocale().formattedDataSize(result.size_bytes));
        name->setData(path, Qt::UserRole);
        model_->appendRow({name.release(), location.release(), size.release()});
    }
    status_->setText(
        reply.results.empty()
            ? (query_->text().isEmpty()
                   ? QStringLiteral("沒有符合條件的檔案；可加入搜尋資料夾，或調整篩選。")
                   : QStringLiteral("找不到符合的檔名。"))
            : (query_->text().isEmpty()
                   ? QStringLiteral("顯示 %1 筆檔案（最多 %2 筆）；輸入關鍵字縮小範圍。")
                   : QStringLiteral("找到 %1 筆結果（最多 %2 筆）。"))
                  .arg(model_->rowCount())
                  .arg(result_limit_));
    if (model_->rowCount() > 0) {
        results_->setCurrentIndex(model_->index(0, 0));
    }
}

void SearchWindow::updatePreviewImage() {
    if (!preview_pixmap_.isNull()) {
        preview_image_->setPixmap(preview_pixmap_.scaled(
            preview_image_->contentsRect().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void SearchWindow::selectResult() {
    const auto current = results_->currentIndex();
    const auto path = current.isValid()
                          ? model_->index(current.row(), 0).data(Qt::UserRole).toString()
                          : QString{};
    selected_path_->setText(preview(path, 2048));
    copy_->setEnabled(!path.isEmpty());
    open_->setEnabled(!path.isEmpty() && !opener_.isBusy() && confirmation_.isNull());
    reveal_->setEnabled(open_->isEnabled());
    requestPreview();
}

void SearchWindow::requestPreview() {
    preview_status_->show();
    preview_text_->clear();
    preview_image_->clear();
    preview_pixmap_ = {};
    preview_image_->hide();
    preview_text_->hide();
    if (!preview_enabled_->isChecked() || !isVisible() || isMinimized()) {
        return;
    }
    const auto current = results_->currentIndex();
    if (!current.isValid()) {
        previewer_.clear();
        preview_status_->setText(
            QStringLiteral("請選取檔案；不支援類型可使用原有確認開啟或複製路徑。"));
        return;
    }
    const auto path = model_->index(current.row(), 0).data(Qt::UserRole).toString();
    preview_status_->setText(QStringLiteral("正在隔離程序中產生唯讀預覽…"));
    previewer_.submit(path);
}

void SearchWindow::requestSelected(FileAction action) {
    if (opener_.isBusy() || !confirmation_.isNull()) {
        return;
    }
    const auto current = results_->currentIndex();
    if (!current.isValid()) {
        return;
    }
    const auto path = model_->index(current.row(), 0).data(Qt::UserRole).toString();
    const bool reveal = action == FileAction::reveal;
    const auto error = reveal ? fileLocationPolicyError(path) : fileOpenPolicyError(path);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setObjectName(reveal ? QStringLiteral("revealConfirmation")
                                 : QStringLiteral("openConfirmation"));
    dialog->setWindowTitle(reveal ? QStringLiteral("確認顯示所在位置")
                                  : QStringLiteral("確認開啟檔案"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(640, 260);
    auto *layout = new QVBoxLayout(dialog);
    auto *label =
        new QLabel(reveal ? QStringLiteral("要在檔案總管選取下列檔案嗎？此動作不執行檔案。")
                          : QStringLiteral("要將下列檔案交由 Windows 預設程式開啟嗎？"),
                   dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *target = new QPlainTextEdit(dialog);
    target->setObjectName(reveal ? QStringLiteral("revealTarget") : QStringLiteral("openTarget"));
    target->setAccessibleName(QStringLiteral("即將處理的完整路徑"));
    target->setReadOnly(true);
    target->setPlainText(path);
    layout->addWidget(target, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, dialog);
    buttons->setObjectName(reveal ? QStringLiteral("revealConfirmationButtons")
                                  : QStringLiteral("openConfirmationButtons"));
    buttons->button(QDialogButtonBox::Open)
        ->setText(reveal ? QStringLiteral("顯示位置") : QStringLiteral("開啟"));
    buttons->button(QDialogButtonBox::Open)->setAutoDefault(false);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, path, action] {
        if (opener_.submit(path, action)) {
            status_->setText(QStringLiteral("正在檢查並送出檔案操作請求…"));
        }
    });
    connect(dialog, &QDialog::finished, this, [this] {
        confirmation_.clear();
        selectResult();
    });
    confirmation_ = dialog;
    selectResult();
    dialog->open();
    cancel->setFocus();
}

void SearchWindow::copyPath() {
    const auto current = results_->currentIndex();
    if (!current.isValid()) {
        return;
    }
    const auto path = model_->index(current.row(), 0).data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        auto *clipboard = QApplication::clipboard();
        clipboard->setText(path);
        if (QApplication::platformName() == QStringLiteral("windows") &&
            !clipboard->ownsClipboard()) {
            status_->setText(QStringLiteral("複製未完成，請稍後重試。"));
            return;
        }
        status_->setText(QStringLiteral("已複製完整路徑。"));
    }
}

bool SearchWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == preview_image_ && event->type() == QEvent::Resize) {
        updatePreviewImage();
    }
    if (watched == results_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier ||
             (key->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier)) {
            requestSelected(key->modifiers().testFlag(Qt::ControlModifier) ? FileAction::reveal
                                                                           : FileAction::open);
            return true;
        }
    }
    if (watched == query_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down && model_->rowCount() > 0) {
            results_->setFocus();
            results_->setCurrentIndex(model_->index(0, 0));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchWindow::hideEvent(QHideEvent *event) {
    preview_enabled_->setChecked(false);
    previewer_.clear();
    QWidget::hideEvent(event);
}

void SearchWindow::closeEvent(QCloseEvent *event) {
    debounce_.stop();
    controller_.cancel();
    opener_.cancel();
    watcher_.cancel();
    indexer_.cancel();
    if (!index_confirmation_.isNull()) {
        index_confirmation_->reject();
    }
    previewer_.clear();
    if (!confirmation_.isNull()) {
        confirmation_->reject();
    }
    updateHotkey(false);
    QWidget::closeEvent(event);
}
} // namespace fileweaver::desktop
