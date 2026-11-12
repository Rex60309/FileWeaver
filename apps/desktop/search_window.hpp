#pragma once

#include "file_open.hpp"
#include "global_hotkey.hpp"
#include "index_controller.hpp"
#include "preferences.hpp"
#include "preview_controller.hpp"
#include "search_controller.hpp"
#include "watch_controller.hpp"
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <utility>

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
class QProgressBar;

namespace fileweaver::desktop {
class SearchWindow final : public QWidget {
    Q_OBJECT
  public:
    explicit SearchWindow(SearchController::Search search = {}, QWidget *parent = nullptr,
                          HotkeyApi hotkey_api = windowsHotkeyApi(),
                          FileOpenController::Open open_file = openSelectedFile,
                          FileOpenController::Open reveal_file = revealSelectedFile,
                          WatchApi watch_api = windowsWatchApi(),
                          IndexApi index_api = windowsIndexApi(), QString managed_database = {});
    void setDatabase(const QString &path);
    void loadPreferences(const QString &path);
    // Always shows a confirmation; choosing a folder alone cannot start a scan.
    void requestIndexFolder(const QString &path);

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
    void updatePreviewImage();
    QPixmap preview_pixmap_;
    QSpinBox *visible_limit_ = nullptr;
    std::vector<std::pair<QCheckBox *, std::vector<std::string>>> type_filters_;
    void showIndexReply(const IndexReply &reply);
    QPushButton *add_folder_;
    QPushButton *choose_database_;
    QPushButton *search_button_;
    QPushButton *index_cancel_;
    QLabel *index_status_;
    QProgressBar *index_progress_;
    QPointer<QDialog> index_confirmation_;
    QString managed_database_;
    QCheckBox *remember_;
    QSpinBox *font_size_;
    QLabel *preferences_status_;
    QString preferences_path_;
    bool applying_preferences_ = false;
    bool preferences_dirty_ = false;
    QCheckBox *preview_enabled_;
    QLabel *preview_status_;
    QLabel *preview_image_ = nullptr;
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
    IndexController indexer_;
    PreviewController previewer_;
    PreferencesController preferences_;
};
} // namespace fileweaver::desktop
