// Regression test for the MainWindow keyboard shortcut table.
//
// The bug this guards against: several actions were created twice with the
// SAME QKeySequence (once in Edit and again in Timeline/Settings/File). Qt
// treats two enabled actions sharing a shortcut in the same context as
// *ambiguous* and fires NEITHER, so `S` (split), `Delete`, `Shift+Delete`,
// `Ctrl+Shift+P` and `Ctrl+,` silently did nothing from the keyboard while
// still looking correct in the menus.
//
// What is verified here:
//   1. No two distinct enabled QActions reachable from the window share a
//      shortcut within the same shortcut context (the ambiguity condition).
//   2. Actions that are meant to appear in several menus really are the same
//      QAction instance, not copies.
//   3. Editing shortcuts that would otherwise shadow text input (S, C,
//      Delete, Ctrl+C/V/D/A, Esc) are scoped to the timeline widget with
//      Qt::WidgetWithChildrenShortcut rather than being window-global.
//   4. Rebuilding the menus (as a language change does) does not accumulate
//      duplicate actions — the leak that would re-introduce ambiguity.
//
// Needs a QApplication and therefore a platform plugin; run it with
// QT_QPA_PLATFORM=offscreen. It creates no project files and writes nothing.

#include <QApplication>
#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>
#include <QSet>
#include <QMap>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QList>
#include <QString>
#include <QStringList>
#include <iostream>
#include <cassert>

#include "../src/ui/MainWindow.h"
#include "../src/i18n/LanguageManager.h"

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "  [ok]   " << what << std::endl;
    } else {
        std::cout << "  [FAIL] " << what << std::endl;
        ++g_failures;
    }
}

// Every action reachable from the menu bar, walking submenus. Qt exposes the
// menu structure, so this mirrors exactly what the user can trigger.
void collectMenuActions(QMenu* menu, QList<QAction*>& out, QSet<QMenu*>& seen) {
    if (!menu || seen.contains(menu)) return;
    seen.insert(menu);
    const auto actions = menu->actions();
    for (QAction* a : actions) {
        if (!a) continue;
        if (a->menu()) {
            collectMenuActions(a->menu(), out, seen);
        } else if (!a->isSeparator()) {
            out.append(a);
        }
    }
}

QList<QAction*> allMenuActions(QMainWindow* w) {
    QList<QAction*> out;
    QSet<QMenu*> seen;
    const auto topLevel = w->menuBar()->actions();
    for (QAction* a : topLevel) {
        if (a->menu()) collectMenuActions(a->menu(), out, seen);
    }
    return out;
}

QString seqText(const QKeySequence& s) { return s.toString(QKeySequence::PortableText); }

} // namespace

int main(int argc, char** argv) {
    // Step 4 switches the UI language, and LanguageManager::setLanguage()
    // persists the choice through QSettings. Redirect all config writes into a
    // throwaway location first so running the test cannot change the real
    // user's saved preferences.
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir configDir;
    if (configDir.isValid()) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    QApplication app(argc, argv);

    std::cout << "Running ShortcutTest..." << std::endl;

    hc::MainWindow window;

    // ---------------------------------------------------------------
    // 1. No ambiguous shortcuts among enabled menu actions.
    // ---------------------------------------------------------------
    std::cout << "[1] Checking for duplicate/ambiguous shortcuts..." << std::endl;
    {
        const QList<QAction*> actions = allMenuActions(&window);
        check(!actions.isEmpty(), "menu bar exposes actions");

        // Key: "context|shortcut". Value: the distinct QActions using it.
        QMap<QString, QList<QAction*>> byShortcut;
        for (QAction* a : actions) {
            const auto shortcuts = a->shortcuts();
            for (const QKeySequence& s : shortcuts) {
                if (s.isEmpty()) continue;
                const QString key =
                    QString::number(static_cast<int>(a->shortcutContext())) + "|" + seqText(s);
                if (!byShortcut[key].contains(a)) byShortcut[key].append(a);
            }
        }

        QStringList clashes;
        for (auto it = byShortcut.begin(); it != byShortcut.end(); ++it) {
            if (it.value().size() > 1) {
                QStringList names;
                for (QAction* a : it.value()) names << a->text();
                clashes << (it.key() + " -> " + names.join(" / "));
            }
        }
        for (const QString& c : clashes) {
            std::cout << "    clash: " << c.toStdString() << std::endl;
        }
        check(clashes.isEmpty(), "no two distinct actions share a shortcut in the same context");
    }

    // ---------------------------------------------------------------
    // 2. Shortcuts that must exist, and must be reachable exactly once.
    // ---------------------------------------------------------------
    std::cout << "[2] Checking documented shortcuts are registered..." << std::endl;
    {
        const QList<QAction*> actions = allMenuActions(&window);
        auto countFor = [&](const QKeySequence& seq) {
            int n = 0;
            for (QAction* a : actions) {
                const auto shortcuts = a->shortcuts();
                for (const QKeySequence& s : shortcuts) {
                    if (s == seq) { ++n; break; }
                }
            }
            return n;
        };

        struct Expect { QKeySequence seq; const char* name; };
        const Expect expected[] = {
            { QKeySequence(Qt::Key_S),                              "S (split at playhead)" },
            { QKeySequence(Qt::Key_C),                              "C (cut tool)" },
            { QKeySequence(QKeySequence::Delete),                   "Delete (delete clip)" },
            { QKeySequence(Qt::SHIFT | Qt::Key_Delete),             "Shift+Delete (delete track)" },
            { QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),       "Ctrl+Shift+P (canvas settings)" },
            { QKeySequence(Qt::CTRL | Qt::Key_Comma),               "Ctrl+, (preferences)" },
            { QKeySequence(Qt::CTRL | Qt::Key_E),                   "Ctrl+E (export)" },
            { QKeySequence(Qt::CTRL | Qt::Key_T),                   "Ctrl+T (add text layer)" },
            { QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L),       "Ctrl+Shift+L (add effect layer)" },
            { QKeySequence(Qt::CTRL | Qt::Key_I),                   "Ctrl+I (import media)" },
            { QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),       "Ctrl+Shift+R (screen record)" },
            { QKeySequence(Qt::SHIFT | Qt::Key_Z),                  "Shift+Z (zoom to fit)" },
            { QKeySequence(Qt::CTRL | Qt::Key_D),                   "Ctrl+D (duplicate clip)" },
            { QKeySequence(Qt::CTRL | Qt::Key_A),                   "Ctrl+A (select first clip)" },
            { QKeySequence(Qt::Key_Escape),                         "Esc (deselect all)" },
        };
        for (const auto& e : expected) {
            const int n = countFor(e.seq);
            check(n == 1, std::string("exactly one action bound to ") + e.name +
                          " (found " + std::to_string(n) + ")");
        }
    }

    // ---------------------------------------------------------------
    // 3. Text-shadowing shortcuts must be timeline-scoped, not global.
    // ---------------------------------------------------------------
    std::cout << "[3] Checking editing shortcuts do not shadow text input..." << std::endl;
    {
        const QList<QAction*> actions = allMenuActions(&window);
        const QList<QKeySequence> mustBeScoped = {
            QKeySequence(Qt::Key_S),
            QKeySequence(Qt::Key_C),
            QKeySequence(QKeySequence::Delete),
            QKeySequence(Qt::SHIFT | Qt::Key_Delete),
            QKeySequence(QKeySequence::Copy),
            QKeySequence(QKeySequence::Paste),
            QKeySequence(Qt::CTRL | Qt::Key_D),
            QKeySequence(Qt::CTRL | Qt::Key_A),
            QKeySequence(Qt::Key_Escape),
        };
        for (const QKeySequence& want : mustBeScoped) {
            for (QAction* a : actions) {
                const auto shortcuts = a->shortcuts();
                bool has = false;
                for (const QKeySequence& s : shortcuts) {
                    if (s == want) { has = true; break; }
                }
                if (!has) continue;
                check(a->shortcutContext() == Qt::WidgetWithChildrenShortcut,
                      seqText(want).toStdString() + " is timeline-scoped, not window-global");
            }
        }
    }

    // ---------------------------------------------------------------
    // 4. Rebuilding menus (language switch) must not duplicate actions.
    // ---------------------------------------------------------------
    std::cout << "[4] Checking menu rebuild does not accumulate duplicates..." << std::endl;
    {
        const int before = allMenuActions(&window).size();

        // A language change clears the menu bar and calls buildMenus() again.
        // Switching to a language and back exercises the rebuild path twice.
        const QString original = hc::LanguageManager::instance().currentLanguage();
        const auto packs = hc::LanguageManager::instance().availableLanguages();
        QString other;
        for (const auto& p : packs) {
            if (p.languageCode != original) { other = p.languageCode; break; }
        }

        if (other.isEmpty()) {
            std::cout << "  [skip] only one language pack available, cannot exercise rebuild"
                      << std::endl;
        } else {
            hc::LanguageManager::instance().setLanguage(other);
            hc::LanguageManager::instance().setLanguage(original);

            const int after = allMenuActions(&window).size();
            check(after == before,
                  "action count stable across two menu rebuilds (" +
                      std::to_string(before) + " -> " + std::to_string(after) + ")");

            // And crucially: still no ambiguity after the rebuild.
            const QList<QAction*> actions = allMenuActions(&window);
            QMap<QString, QList<QAction*>> byShortcut;
            for (QAction* a : actions) {
                const auto shortcuts = a->shortcuts();
                for (const QKeySequence& s : shortcuts) {
                    if (s.isEmpty()) continue;
                    const QString key =
                        QString::number(static_cast<int>(a->shortcutContext())) + "|" + seqText(s);
                    if (!byShortcut[key].contains(a)) byShortcut[key].append(a);
                }
            }
            bool clash = false;
            for (auto it = byShortcut.begin(); it != byShortcut.end(); ++it) {
                if (it.value().size() > 1) {
                    clash = true;
                    std::cout << "    clash after rebuild: " << it.key().toStdString() << std::endl;
                }
            }
            check(!clash, "no ambiguous shortcuts after rebuilding the menus");
        }
    }

    std::cout << std::endl;
    if (g_failures == 0) {
        std::cout << "ShortcutTest: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cout << "ShortcutTest: " << g_failures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
