#include "search_window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
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
#include <QStandardItemModel>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
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
                           FileOpenController::Open open_file)
    : QWidget(parent), hotkey_(std::move(hotkey_api)), controller_(std::move(search)),
      opener_(std::move(open_file)) {
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
    mode_->addItems(
        {QStringLiteral("包含文字"), QStringLiteral("檔名開頭"), QStringLiteral("完整檔名")});
    auto *search_button = new QPushButton(QStringLiteral("搜尋"), this);
    search_button->setObjectName(QStringLiteral("search"));
    search_row->addWidget(query_, 1);
    search_row->addWidget(mode_);
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
    selection_row->addWidget(copy_);
    layout->addLayout(selection_row.release());
    layout->addWidget(new QLabel(
        QStringLiteral("↓ 進入結果 · ↑ ↓ 選取 · Enter 開啟確認 · Ctrl+C 複製路徑 · Esc 關閉"),
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
    connect(open_, &QPushButton::clicked, this, &SearchWindow::requestOpenSelected);
    connect(results_, &QTreeView::doubleClicked, this, [this] { requestOpenSelected(); });
    connect(&opener_, &FileOpenController::completed, this, [this](const OpenReply &reply) {
        status_->setText(QStringLiteral("上次開啟請求：") + preview(reply.message, 512));
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
    database_ = path;
    database_path_->setText(QDir::toNativeSeparators(path));
    schedule();
    query_->setFocus();
}

void SearchWindow::clearResults() {
    model_->removeRows(0, model_->rowCount());
    selected_path_->clear();
    copy_->setEnabled(false);
    open_->setEnabled(false);
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
    request.mode = mode_->currentIndex() == 1   ? SearchMode::prefix
                   : mode_->currentIndex() == 2 ? SearchMode::exact
                                                : SearchMode::substring;
    status_->setText(QStringLiteral("正在搜尋…"));
    controller_.submit(std::move(request));
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
}

void SearchWindow::requestOpenSelected() {
    if (opener_.isBusy() || !confirmation_.isNull()) {
        return;
    }
    const auto current = results_->currentIndex();
    if (!current.isValid()) {
        return;
    }
    const auto path = model_->index(current.row(), 0).data(Qt::UserRole).toString();
    const auto error = fileOpenPolicyError(path);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("openConfirmation"));
    dialog->setWindowTitle(QStringLiteral("確認開啟檔案"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(640, 260);
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(QStringLiteral("要將下列檔案交由 Windows 預設程式開啟嗎？"), dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *target = new QPlainTextEdit(dialog);
    target->setObjectName(QStringLiteral("openTarget"));
    target->setAccessibleName(QStringLiteral("即將開啟的完整路徑"));
    target->setReadOnly(true);
    target->setPlainText(path);
    layout->addWidget(target, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, dialog);
    buttons->setObjectName(QStringLiteral("openConfirmationButtons"));
    buttons->button(QDialogButtonBox::Open)->setText(QStringLiteral("開啟"));
    buttons->button(QDialogButtonBox::Open)->setAutoDefault(false);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, path] {
        if (opener_.submit(path)) {
            status_->setText(QStringLiteral("正在檢查並送出開啟請求…"));
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
            (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            requestOpenSelected();
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
    if (!confirmation_.isNull()) {
        confirmation_->reject();
    }
    updateHotkey(false);
    QWidget::closeEvent(event);
}
} // namespace fileweaver::desktop
