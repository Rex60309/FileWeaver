#pragma once

#include "file_open.hpp"
#include "global_hotkey.hpp"
#include "preferences.hpp"
#include "preview_controller.hpp"
#include "search_controller.hpp"
#include "watch_controller.hpp"
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QDialog;
class QCheckBox;
class QKeySequenceEdit;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
class QTreeView;
class QPlainTextEdit;
class QSpinBox;
class QEvent;

namespace fileweaver::desktop {
class SearchWindow final : public QWidget {
    Q_OBJECT
  public:
    explicit SearchWindow(SearchController::Search search = fileweaver::search_files,
                          QWidget *parent = nullptr, HotkeyApi hotkey_api = windowsHotkeyApi(),
                          FileOpenController::Open open_file = openSelectedFile,
                          FileOpenController::Open reveal_file = revealSelectedFile,
                          WatchApi watch_api = windowsWatchApi());
    void setDatabase(const QString &path);
    void loadPreferences(const QString &path);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void schedule();
    void searchNow();
    void clearResults();
    void showReply(const SearchReply &reply);
    void selectResult();
    void activateSearch();
    void updateHotkey(bool enabled);
    void copyPath();
    void requestSelected(FileAction action);
    void configureFuzzy();
    void configureFilters();
    void requestPreview();
    void preferenceChanged();
    void savePreferences();
    QCheckBox *remember_;
    QSpinBox *font_size_;
    QLabel *preferences_status_;
    QString preferences_path_;
    bool applying_preferences_ = false;
    bool preferences_dirty_ = false;
    QCheckBox *preview_enabled_;
    QLabel *preview_status_;
    QLabel *preview_image_;
    QPlainTextEdit *preview_text_ = nullptr;
    QWidget *preview_pane_;
    SearchFilters filters_{};
    std::optional<SearchSort> sort_;
    bool descending_ = false;
    std::size_t result_limit_ = 100;
    void updateWatchControls();
    void showWatchReply(const WatchReply &reply);
    std::vector<RootInfo> watch_roots_;
    std::uint64_t watch_scans_ = 0;
    std::optional<std::int64_t> watch_last_sync_;
    QPushButton *watch_load_;
    QComboBox *watch_root_;
    QPushButton *watch_start_;
    QPushButton *watch_stop_;
    QLabel *watch_status_;
    FuzzyOptions fuzzy_{};
    QString database_;
    // Non-owning pointers: widgets/model are owned immediately by their Qt parent.
    QLineEdit *database_path_;
    QLineEdit *query_;
    QLineEdit *selected_path_;
    QComboBox *mode_;
    QLabel *status_;
    QTreeView *results_;
    QStandardItemModel *model_;
    QPushButton *copy_;
    QPushButton *open_;
    QPushButton *reveal_;
    QPointer<QDialog> confirmation_;
    QCheckBox *hotkey_enabled_;
    QKeySequenceEdit *hotkey_sequence_;
    QLabel *hotkey_status_;
    GlobalHotkey hotkey_;
    QTimer debounce_;
    SearchController controller_;
    FileOpenController opener_;
    WatchController watcher_;
    PreviewController previewer_;
    PreferencesController preferences_;
};
} // namespace fileweaver::desktop
