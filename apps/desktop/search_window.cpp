#include "search_window.hpp"
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
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimeZone>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
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
                           WatchApi watch_api)
    : QWidget(parent), hotkey_(std::move(hotkey_api)), controller_(std::move(search)),
      opener_(std::move(open_file), nullptr, std::move(reveal_file)),
      watcher_(std::move(watch_api)) {
    setWindowTitle(QStringLiteral("FileWeaver — 檔名搜尋"));
    setWindowIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    resize(1000, 650);
    setMinimumSize(760, 480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto *heading = new QLabel(QStringLiteral("FileWeaver"), this);
    auto font = heading->font();
    font.setPointSize(22);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(new QLabel(QStringLiteral("在本機索引中尋找檔名"), this));
    auto database_row = std::make_unique<QHBoxLayout>();
    database_path_ = new QLineEdit(this);
    database_path_->setObjectName(QStringLiteral("databasePath"));
    database_path_->setAccessibleName(QStringLiteral("目前索引"));
    database_path_->setPlaceholderText(QStringLiteral("尚未選擇索引"));
    database_path_->setReadOnly(true);
    auto *choose = new QPushButton(QStringLiteral("選擇索引…"), this);
    choose->setObjectName(QStringLiteral("chooseDatabase"));
    database_row->addWidget(database_path_, 1);
    database_row->addWidget(choose);
    layout->addLayout(database_row.release());
    auto search_row = std::make_unique<QHBoxLayout>();
    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("query"));
    query_->setAccessibleName(QStringLiteral("檔名搜尋"));
    query_->setPlaceholderText(QStringLiteral("輸入檔名，例如 報告 或 日本語"));
    query_->setClearButtonEnabled(true);
    query_->setMaxLength(4096);
    query_->installEventFilter(this);
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("mode"));
    mode_->setAccessibleName(QStringLiteral("比對方式"));
    mode_->addItems({QStringLiteral("包含文字"), QStringLiteral("檔名開頭"),
                     QStringLiteral("完整檔名"), QStringLiteral("模糊檔名")});
    auto *fuzzy_settings = new QPushButton(QStringLiteral("模糊設定…"), this);
    fuzzy_settings->setObjectName(QStringLiteral("fuzzySettingsButton"));
    fuzzy_settings->setEnabled(false);
    connect(fuzzy_settings, &QPushButton::clicked, this, &SearchWindow::configureFuzzy);
    connect(mode_, &QComboBox::currentIndexChanged, this,
            [fuzzy_settings](int index) { fuzzy_settings->setEnabled(index == 3); });
    auto *search_button = new QPushButton(QStringLiteral("搜尋"), this);
    search_button->setObjectName(QStringLiteral("search"));
    search_row->addWidget(query_, 1);
    search_row->addWidget(mode_);
    search_row->addWidget(fuzzy_settings);
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
    layout->addLayout(hotkey_row.release());
    hotkey_status_ = new QLabel(QStringLiteral("快捷鍵未啟用；設定僅供本次執行使用。"), this);
    hotkey_status_->setObjectName(QStringLiteral("hotkeyStatus"));
    hotkey_status_->setTextFormat(Qt::PlainText);
    hotkey_status_->setWordWrap(true);
    layout->addWidget(hotkey_status_);
    connect(hotkey_enabled_, &QCheckBox::toggled, this, &SearchWindow::updateHotkey);
    connect(&hotkey_, &GlobalHotkey::activated, this, &SearchWindow::activateSearch);
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
    layout->addLayout(watch_row.release());
    watch_status_ = new QLabel(QStringLiteral("監控未啟用；只有手動啟用後才會背景重掃。"), this);
    watch_status_->setObjectName(QStringLiteral("watchStatus"));
    watch_status_->setTextFormat(Qt::PlainText);
    watch_status_->setWordWrap(true);
    layout->addWidget(watch_status_);
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
    status_ = new QLabel(QStringLiteral("選擇現有索引後，即可開始搜尋。"), this);
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
    layout->addWidget(results_, 1);
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
    layout->addWidget(new QLabel(QStringLiteral("↓ 進入結果 · ↑ ↓ 選取 · Enter 開啟確認 · "
                                                "Ctrl+Enter 顯示位置 · Ctrl+C 複製路徑 · Esc 關閉"),
                                 this));
    auto *note =
        new QLabel(QStringLiteral("結果來自索引；檔案變更後請更新索引。最多顯示 100 筆。"), this);
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
    connect(results_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { selectResult(); });
    connect(&controller_, &SearchController::completed, this, &SearchWindow::showReply);
    auto *copy_shortcut = new QShortcut(QKeySequence::Copy, results_);
    copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy_shortcut, &QShortcut::activated, this, &SearchWindow::copyPath);
    auto *close_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(close_shortcut, &QShortcut::activated, this, &SearchWindow::close);
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
            : QStringLiteral("快捷鍵未啟用；設定僅供本次執行使用。"));
}

void SearchWindow::activateSearch() {
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    query_->setFocus(Qt::ShortcutFocusReason);
    query_->selectAll();
}

void SearchWindow::setDatabase(const QString &path) {
    watcher_.cancel();
    watch_roots_.clear();
    watch_root_->clear();
    watch_scans_ = 0;
    watch_last_sync_.reset();
    watch_status_->setText(QStringLiteral("索引已切換；監控預設關閉。"));
    database_ = path;
    updateWatchControls();
    database_path_->setText(QDir::toNativeSeparators(path));
    schedule();
    query_->setFocus();
}

void SearchWindow::updateWatchControls() {
    const auto busy = watcher_.isBusy();
    watch_load_->setEnabled(!database_.isEmpty() && !busy);
    watch_root_->setEnabled(!busy && !watch_roots_.empty());
    const auto index = watch_root_->currentIndex();
    const bool known = index >= 0 && static_cast<std::size_t>(index) < watch_roots_.size() &&
                       watch_roots_[static_cast<std::size_t>(index)].policy_known;
    watch_start_->setEnabled(!busy && known);
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
    if (database_.isEmpty()) {
        status_->setText(QStringLiteral("請先選擇現有索引。"));
    } else if (query_->text().isEmpty()) {
        status_->setText(QStringLiteral("輸入檔名開始搜尋。"));
    } else {
        status_->setText(QStringLiteral("正在準備搜尋…"));
        debounce_.start();
    }
}

void SearchWindow::searchNow() {
    debounce_.stop();
    controller_.cancel();
    clearResults();
    if (database_.isEmpty() || query_->text().isEmpty()) {
        schedule();
        return;
    }
    const auto query = query_->text().toUtf8();
    if (query.size() > 4096) {
        status_->setText(QStringLiteral("搜尋文字太長，請縮短後重試。"));
        return;
    }
    SearchRequest request{std::filesystem::path(database_.toStdWString()), query.toStdString()};
    request.mode = mode_->currentIndex() == 3   ? SearchMode::fuzzy
                   : mode_->currentIndex() == 1 ? SearchMode::prefix
                   : mode_->currentIndex() == 2 ? SearchMode::exact
                                                : SearchMode::substring;
    if (request.mode == SearchMode::fuzzy) {
        request.sort = SearchSort::relevance;
        request.fuzzy = fuzzy_;
    }
    status_->setText(QStringLiteral("正在搜尋…"));
    controller_.submit(std::move(request));
}

void SearchWindow::configureFuzzy() {
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("fuzzySettings"));
    dialog->setWindowTitle(QStringLiteral("模糊搜尋設定（僅本次執行）"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    auto *layout = new QFormLayout(dialog);
    const std::array<QString, 6> labels{
        QStringLiteral("完整檔名加分"), QStringLiteral("檔名開頭加分"),
        QStringLiteral("詞片段分數"),   QStringLiteral("子序列分數"),
        QStringLiteral("拼字容錯分數"), QStringLiteral("候選上限")};
    const std::array<int, 6> values{fuzzy_.weights.exact, fuzzy_.weights.prefix,
                                    fuzzy_.weights.token, fuzzy_.weights.subsequence,
                                    fuzzy_.weights.typo,  static_cast<int>(fuzzy_.candidate_limit)};
    std::array<QSpinBox *, 6> fields{};
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
                          fields[3]->value(), fields[4]->value()};
        fuzzy_.candidate_limit = static_cast<std::size_t>(fields[5]->value());
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
            ? QStringLiteral("找不到符合的檔名。")
            : QStringLiteral("找到 %1 筆結果（最多 100 筆）。").arg(model_->rowCount()));
    if (model_->rowCount() > 0) {
        results_->setCurrentIndex(model_->index(0, 0));
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

void SearchWindow::closeEvent(QCloseEvent *event) {
    debounce_.stop();
    controller_.cancel();
    opener_.cancel();
    watcher_.cancel();
    if (!confirmation_.isNull()) {
        confirmation_->reject();
    }
    updateHotkey(false);
    QWidget::closeEvent(event);
}
} // namespace fileweaver::desktop
