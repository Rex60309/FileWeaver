#pragma once

#include "file_open.hpp"
#include "global_hotkey.hpp"
#include "search_controller.hpp"
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

namespace fileweaver::desktop {
class SearchWindow final : public QWidget {
    Q_OBJECT
  public:
    explicit SearchWindow(SearchController::Search search = fileweaver::search_files,
                          QWidget *parent = nullptr, HotkeyApi hotkey_api = windowsHotkeyApi(),
                          FileOpenController::Open open_file = openSelectedFile);
    void setDatabase(const QString &path);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void schedule();
    void searchNow();
    void clearResults();
    void showReply(const SearchReply &reply);
    void selectResult();
    void activateSearch();
    void updateHotkey(bool enabled);
    void copyPath();
    void requestOpenSelected();
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
    QPointer<QDialog> confirmation_;
    QCheckBox *hotkey_enabled_;
    QKeySequenceEdit *hotkey_sequence_;
    QLabel *hotkey_status_;
    GlobalHotkey hotkey_;
    QTimer debounce_;
    SearchController controller_;
    FileOpenController opener_;
};
} // namespace fileweaver::desktop
