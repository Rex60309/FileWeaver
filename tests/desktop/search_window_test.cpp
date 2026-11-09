#include "arguments.hpp"
#include "fileweaver/application/index.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include "search_controller.hpp"
#include "search_window.hpp"
#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <windows.h>

using fileweaver::desktop::GlobalHotkey;
using fileweaver::desktop::HotkeyApi;
using fileweaver::desktop::HotkeyBinding;
using fileweaver::desktop::SearchController;
using fileweaver::desktop::SearchReply;
using fileweaver::desktop::SearchWindow;

namespace {
template <class T> T *control(SearchWindow &window, const char *name) {
    auto *found = window.findChild<T *>(QString::fromLatin1(name));
    if (!found) {
        throw std::runtime_error("Required desktop control missing");
    }
    return found;
}
fileweaver::SearchResult record(const std::string &name) {
    return {1, 1, "C:/synthetic/" + name, name, 3};
}
} // namespace

class DesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void hotkeyThreadMessageReachesQtDispatcher() {
        HotkeyBinding binding{};
        GlobalHotkey hotkey(HotkeyApi{[&](const HotkeyBinding &request) {
                                          binding = request;
                                          return ERROR_SUCCESS;
                                      },
                                      [](int) { return ERROR_SUCCESS; }});
        QVERIFY(hotkey.enable(QKeySequence(QStringLiteral("Ctrl+Alt+Space"))).isEmpty());
        QSignalSpy activated(&hotkey, &GlobalHotkey::activated);
        const auto parameter = MAKELPARAM(
            static_cast<WORD>(binding.modifiers & ~static_cast<unsigned int>(MOD_NOREPEAT)),
            static_cast<WORD>(binding.virtual_key));
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY,
                                   static_cast<WPARAM>(binding.identifier), parameter) != 0);
        QTRY_COMPARE(activated.count(), 1);
    }

    void hotkeyValidationMessagesAndCleanup() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, (GlobalHotkey{HotkeyApi{}}));
        int calls = 0;
        int releases = 0;
        int captured_id = 0;
        std::vector<int> released_ids;
        unsigned int captured_modifiers = 0;
        unsigned int captured_key = 0;
        unsigned long registration_error = ERROR_HOTKEY_ALREADY_REGISTERED;
        unsigned long cleanup_error = ERROR_SUCCESS;
        HotkeyApi api{[&](const HotkeyBinding &binding) {
                          ++calls;
                          captured_id = binding.identifier;
                          captured_modifiers = binding.modifiers;
                          captured_key = binding.virtual_key;
                          return registration_error;
                      },
                      [&](int id) {
                          released_ids.push_back(id);
                          ++releases;
                          return cleanup_error;
                      }};
        {
            GlobalHotkey hotkey(api);
            QVERIFY(!hotkey.isEnabled());
            QCOMPARE(calls, 0);
            for (const auto &text :
                 {"", "F3", "Shift+A", "Meta+Space", "Ctrl+F12", "Ctrl+Left", "Ctrl+A, Ctrl+B"}) {
                QVERIFY(!hotkey.enable(QKeySequence(QString::fromLatin1(text))).isEmpty());
            }
            QCOMPARE(calls, 0);
            const QKeySequence chord(QStringLiteral("Ctrl+Alt+Space"));
            QVERIFY(hotkey.enable(chord).contains(QStringLiteral("占用")));
            QVERIFY(!hotkey.isEnabled());
            registration_error = ERROR_SUCCESS;
            QVERIFY(hotkey.enable(chord).isEmpty());
            QCOMPARE(captured_modifiers,
                     static_cast<unsigned int>(MOD_CONTROL | MOD_ALT | MOD_NOREPEAT));
            QCOMPARE(captured_key, static_cast<unsigned int>(VK_SPACE));
            QVERIFY(!hotkey.enable(chord).isEmpty());
            QCOMPARE(calls, 2);
            QSignalSpy activated(&hotkey, &GlobalHotkey::activated);
            MSG message{};
            message.message = WM_HOTKEY;
            message.wParam = static_cast<WPARAM>(captured_id);
            message.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT, VK_SPACE);
            qintptr result = 7;
            QVERIFY(!hotkey.nativeEventFilter("unrelated_native_event", &message, &result));
            QVERIFY(!hotkey.nativeEventFilter("windows_dispatcher_MSG", nullptr, &result));
            message.wParam += 1;
            QVERIFY(!hotkey.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            message.wParam -= 1;
            message.lParam = MAKELPARAM(MOD_CONTROL, VK_SPACE);
            QVERIFY(!hotkey.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            QCOMPARE(activated.count(), 0);
            QCOMPARE(result, qintptr{7});
            message.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT, VK_SPACE);
            QVERIFY(hotkey.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            QCOMPARE(activated.count(), 1);
            QCOMPARE(result, qintptr{0});
            cleanup_error = ERROR_ACCESS_DENIED;
            QVERIFY(!hotkey.disable().isEmpty());
            QVERIFY(hotkey.isEnabled());
            cleanup_error = ERROR_SUCCESS;
            QVERIFY(hotkey.disable().isEmpty());
            QVERIFY(!hotkey.isEnabled());
            QVERIFY(!hotkey.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            QCOMPARE(activated.count(), 1);
            QVERIFY(hotkey.enable(QKeySequence(QStringLiteral("Ctrl+Shift+F24"))).isEmpty());
            QCOMPARE(captured_key, static_cast<unsigned int>(VK_F24));
            QCOMPARE(captured_modifiers,
                     static_cast<unsigned int>(MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT));
        }
        QCOMPARE(releases, 3); // Failed disable, successful retry, destructor cleanup.
        QVERIFY(released_ids == (std::vector<int>{captured_id, captured_id, captured_id}));
    }

    void hotkeyOptInConflictRetryAndFocus() {
        int calls = 0;
        int releases = 0;
        unsigned long registration_error = ERROR_HOTKEY_ALREADY_REGISTERED;
        unsigned long cleanup_error = ERROR_SUCCESS;
        int captured_id = 0;
        unsigned int captured_modifiers = 0;
        unsigned int captured_key = 0;
        HotkeyApi api{[&](const HotkeyBinding &binding) {
                          ++calls;
                          captured_id = binding.identifier;
                          captured_modifiers = binding.modifiers;
                          captured_key = binding.virtual_key;
                          return registration_error;
                      },
                      [&](int) {
                          ++releases;
                          return cleanup_error;
                      }};
        SearchWindow window(fileweaver::search_files, nullptr, api);
        window.show();
        auto *enabled = control<QCheckBox>(window, "hotkeyEnabled");
        auto *sequence = control<QKeySequenceEdit>(window, "hotkeySequence");
        auto *status = control<QLabel>(window, "hotkeyStatus");
        auto *query = control<QLineEdit>(window, "query");
        QCOMPARE(calls, 0);
        QVERIFY(!enabled->isChecked());
        enabled->setChecked(true);
        QVERIFY(!enabled->isChecked());
        QVERIFY(sequence->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("占用")));
        registration_error = ERROR_SUCCESS;
        enabled->setChecked(true);
        QVERIFY(enabled->isChecked());
        QVERIFY(!sequence->isEnabled());
        query->setText(QStringLiteral("保留的查詢 日本語"));
        window.showMinimized();
        QVERIFY(window.isMinimized());
        MSG message{};
        message.message = WM_HOTKEY;
        message.wParam = static_cast<WPARAM>(captured_id);
        message.lParam = MAKELPARAM(
            static_cast<WORD>(captured_modifiers & ~static_cast<unsigned int>(MOD_NOREPEAT)),
            static_cast<WORD>(captured_key));
        qintptr result = 1;
        QVERIFY(QAbstractEventDispatcher::instance()->filterNativeEvent("windows_dispatcher_MSG",
                                                                        &message, &result));
        QTRY_VERIFY(!window.isMinimized());
        QTRY_VERIFY(query->hasFocus());
        QCOMPARE(query->selectedText(), QStringLiteral("保留的查詢 日本語"));
        QCOMPARE(query->text(), QStringLiteral("保留的查詢 日本語"));
        cleanup_error = ERROR_ACCESS_DENIED;
        enabled->setChecked(false);
        QVERIFY(enabled->isChecked());
        QVERIFY(!sequence->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("無法停用")));
        cleanup_error = ERROR_SUCCESS;
        enabled->setChecked(false);
        QVERIFY(!enabled->isChecked());
        QVERIFY(sequence->isEnabled());
        QCOMPARE(releases, 2);
        window.showNormal();
        sequence->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+F23")));
        enabled->setChecked(true);
        QCOMPARE(calls, 3);
        window.close();
        QCOMPARE(releases, 3);
        QVERIFY(!enabled->isChecked());
    }

    void startupArguments() {
        QCommandLineParser parser;
        fileweaver::desktop::configureArguments(parser);
        QVERIFY(parser.parse({QStringLiteral("fileweaver-desktop")}));
        QVERIFY(!parser.isSet(QStringLiteral("database")));
        const auto database = QStringLiteral("C:/合成資料 日本語😀/索引.sqlite");
        QVERIFY(parser.parse(
            {QStringLiteral("fileweaver-desktop"), QStringLiteral("--database"), database}));
        QCOMPARE(parser.value(QStringLiteral("database")), database);
        QVERIFY(
            !parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--database")}));
        QVERIFY(!parser.errorText().isEmpty());
        QVERIFY(!parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--unknown")}));
        QVERIFY(parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--help")}));
        QVERIFY(parser.isSet(QStringLiteral("help")));
        QVERIFY(parser.helpText().contains(QStringLiteral("--database")));
    }
    void doesNotSearchImplicitly() {
        std::atomic<int> calls = 0;
        SearchWindow window([&](const auto &, const auto &) {
            ++calls;
            return std::vector<fileweaver::SearchResult>{};
        });
        window.show();
        auto *query = control<QLineEdit>(window, "query");
        query->setText(QStringLiteral("demo"));
        QTest::qWait(230);
        QCOMPARE(calls.load(), 0);
        query->clear();
        window.setDatabase(QStringLiteral("C:/synthetic/never-opened.sqlite"));
        QTest::qWait(230);
        QCOMPARE(calls.load(), 0);
        QVERIFY(!control<QPushButton>(window, "copyPath")->isEnabled());
    }

    void unicodeModesKeyboardCopyAndOfflineSnapshot() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto base = std::filesystem::path(temporary.path().toStdWString());
        const auto root = base / std::filesystem::path(u8"\u8CC7\u6599 source");
        std::filesystem::create_directory(root);
        const std::vector<std::u8string> names{u8"\u5831\u544A_\u65E5\u672C\u8A9E\U0001F600.txt",
                                               u8"x\u5831\u544A_\u65E5\u672C\u8A9E.txt",
                                               u8"notes.md"};
        for (const auto &name : names) {
            std::ofstream file(root / std::filesystem::path(name));
            file << "abc";
        }
        const auto database = base / std::filesystem::path(u8"\u7D22\u5F15\U0001F600.sqlite");
        fileweaver::initialize_database(database);
        QCOMPARE(
            fileweaver::index_directory({database, root, {}}, [] { return std::int64_t{1}; }).added,
            std::int64_t{3});
        const auto before = fileweaver::catalog_status(database);
        const auto selected = QString::fromStdWString(
            std::filesystem::canonical(root / std::filesystem::path(names[0])).native());
        SearchWindow window;
        window.setDatabase(QString::fromStdWString(database.native()));
        window.show();
        window.activateWindow();
        auto *query = control<QLineEdit>(window, "query");
        auto *tree = control<QTreeView>(window, "results");
        auto *mode = control<QComboBox>(window, "mode");
        query->setText(QStringLiteral("報告"));
        QTRY_COMPARE(tree->model()->rowCount(), 2);
        query->setFocus();
        QTest::keyClick(query, Qt::Key_Down);
        QVERIFY(tree->hasFocus());
        QCOMPARE(tree->currentIndex().row(), 0);
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(tree->currentIndex().row(), 1);
        QTest::mouseClick(control<QPushButton>(window, "copyPath"), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(selected));
        QApplication::clipboard()->setText(QStringLiteral("synthetic clipboard sentinel"));
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(selected));
        mode->setCurrentIndex(1);
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QCOMPARE(tree->model()->index(0, 0).data().toString(), QStringLiteral("報告_日本語😀.txt"));
        mode->setCurrentIndex(2);
        query->setText(QStringLiteral("報告_日本語😀.txt"));
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        query->clear();
        QCOMPARE(tree->model()->rowCount(), 0);
        QVERIFY(!control<QPushButton>(window, "copyPath")->isEnabled());
        std::filesystem::rename(root, base / "offline-source");
        mode->setCurrentIndex(0);
        query->setText(QStringLiteral("報告"));
        QTRY_COMPARE(tree->model()->rowCount(), 2);
        const auto after = fileweaver::catalog_status(database);
        QCOMPARE(after.scan_records, before.scan_records);
        QCOMPARE(after.present_files, before.present_files);
        QTest::keyClick(query, Qt::Key_Escape);
        QTRY_VERIFY(!window.isVisible());
    }

    void missingDatabaseIsAnErrorWithoutCreation() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto database = temporary.path() + QStringLiteral("/missing.sqlite");
        SearchWindow window;
        window.setDatabase(database);
        window.show();
        control<QLineEdit>(window, "query")->setText(QStringLiteral("report"));
        QTRY_VERIFY(
            control<QLabel>(window, "status")->text().startsWith(QStringLiteral("無法搜尋：")));
        QCOMPARE(control<QTreeView>(window, "results")->model()->rowCount(), 0);
        QVERIFY(!std::filesystem::exists(std::filesystem::path(database.toStdWString())));
    }

    void latestPendingWinsAndGuiStaysResponsive() {
        struct State {
            QSemaphore gate;
            std::atomic<bool> entered = false;
            std::atomic<bool> cancelled = false;
            std::atomic<int> active = 0;
            std::atomic<bool> overlapped = false;
            std::mutex mutex;
            std::vector<std::string> calls;
        };
        auto state = std::make_shared<State>();
        SearchController controller([state](const auto &request, const auto &stop) {
            if (state->active.fetch_add(1) != 0) {
                state->overlapped = true;
            }
            {
                const std::lock_guard lock(state->mutex);
                state->calls.push_back(request.query);
            }
            if (request.query == "old") {
                state->entered = true;
                state->gate.acquire(); // Deliberately return a stale success after cancellation.
                state->cancelled = stop.stop_requested();
            }
            state->active.fetch_sub(1);
            return std::vector<fileweaver::SearchResult>{record(request.query)};
        });
        const auto release = qScopeGuard([state] { state->gate.release(); });
        QSignalSpy completed(&controller, &SearchController::completed);
        controller.submit({{}, "old"});
        QTRY_VERIFY(state->entered.load());
        bool heartbeat = false;
        QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        controller.submit({{}, "middle"});
        controller.submit({{}, "latest"});
        state->gate.release();
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = qvariant_cast<SearchReply>(completed.at(0).at(0));
        QCOMPARE(reply.results.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(reply.results[0].name), QStringLiteral("latest"));
        QVERIFY(state->cancelled.load());
        QVERIFY(!state->overlapped.load());
        const std::lock_guard lock(state->mutex);
        QVERIFY(state->calls == (std::vector<std::string>{"old", "latest"}));
    }

    void cancellationOnDestructionAndInvalidUtf8() {
        std::atomic<bool> entered = false;
        std::atomic<bool> exited = false;
        auto controller = std::make_unique<SearchController>([&](const auto &, const auto &stop) {
            std::mutex mutex;
            std::condition_variable_any condition;
            std::unique_lock lock(mutex);
            entered = true;
            condition.wait(lock, stop, [] { return false; });
            exited = true;
            return std::vector<fileweaver::SearchResult>{};
        });
        controller->submit({{}, "waiting"});
        QTRY_VERIFY(entered.load());
        controller.reset();
        QVERIFY(exited.load());
        SearchController invalid([](const auto &, const auto &) {
            return std::vector<fileweaver::SearchResult>{record(std::string("\xff", 1))};
        });
        QSignalSpy completed(&invalid, &SearchController::completed);
        invalid.submit({{}, "invalid"});
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = qvariant_cast<SearchReply>(completed.at(0).at(0));
        QVERIFY(!reply.error.isEmpty());
        QVERIFY(reply.results.empty());
    }
};

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    if (QApplication::platformName() != QStringLiteral("minimal")) {
        return 2; // Never run clipboard assertions against the user's native clipboard.
    }
    DesktopTests tests;
    return QTest::qExec(&tests, QApplication::arguments());
}

#include "search_window_test.moc"
