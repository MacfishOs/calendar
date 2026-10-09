// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "calendarstore.h"
#include "cutefishapplication.h"
#include "preferences.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

static QStringList qmlWarnings;
static QtMessageHandler previousHandler = nullptr;

static void captureWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && (message.contains(QLatin1String("qrc:/qml/")) || message.contains(QLatin1String("Binding loop"))))
        qmlWarnings.append(message);
    if (previousHandler)
        previousHandler(type, context, message);
}

static QList<QQuickItem *> findItems(QQuickItem *root, const QString &objectName)
{
    QList<QQuickItem *> found;
    // Views kept for later are hidden by opacity.
    if (!root || root->opacity() == 0)
        return found;
    if (root->objectName() == objectName && root->isVisible())
        found.append(root);
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found += findItems(child, objectName);
    return found;
}

class UiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void views_data();
    void views();
    void eventsShowInEveryView();
    void editorCreatesEvent();
    void searchFindsEvent();
    void sidebarFollowsWidth();

private:
    bool load();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<CalendarStore> m_store;
    std::unique_ptr<Application> m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
};

void UiTest::initTestCase()
{
    previousHandler = qInstallMessageHandler(captureWarnings);
    QStandardPaths::setTestModeEnabled(true);
}

void UiTest::cleanupTestCase()
{
    qInstallMessageHandler(previousHandler);
}

void UiTest::init()
{
    QFile::remove(Preferences::ownFile());
    qmlWarnings.clear();
    m_dir = std::make_unique<QTemporaryDir>();
    m_store = std::make_unique<CalendarStore>(m_dir->path());
    m_app = std::make_unique<Application>(m_store.get());
    m_store->load();
}

void UiTest::cleanup()
{
    m_engine.reset();
    m_window = nullptr;
    m_app.reset();
    m_store.reset();
    m_dir.reset();
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
}

bool UiTest::load()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(QStringLiteral("App"), m_app.get());
    m_engine->load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (m_engine->rootObjects().isEmpty())
        return false;
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    return m_window && QTest::qWaitForWindowExposed(m_window);
}

void UiTest::views_data()
{
    QTest::addColumn<QString>("view");
    QTest::addColumn<QString>("objectName");
    QTest::addColumn<bool>("dark");
    for (bool dark : { false, true }) {
        const char *mode = dark ? " dark" : " light";
        QTest::newRow(qPrintable(QStringLiteral("day") + mode)) << "day" << "dayView" << dark;
        QTest::newRow(qPrintable(QStringLiteral("week") + mode)) << "week" << "weekView" << dark;
        QTest::newRow(qPrintable(QStringLiteral("month") + mode)) << "month" << "monthView" << dark;
        QTest::newRow(qPrintable(QStringLiteral("year") + mode)) << "year" << "yearView" << dark;
    }
}

void UiTest::views()
{
    QFETCH(QString, view);
    QFETCH(QString, objectName);
    QFETCH(bool, dark);
    m_app->setView(view);
    QVERIFY(load());
    if (dark) {
        QObject *theme = m_engine->singletonInstance<QObject *>("FishUI", "Theme");
        if (theme)
            theme->setProperty("darkMode", true);
    }
    QTRY_COMPARE(findItems(m_window->contentItem(), objectName).size(), 1);
    QVERIFY(!findItems(m_window->contentItem(), QStringLiteral("periodTitle")).isEmpty());

    // Every view steps forward and back to where it was.
    const QDateTime range = m_window->property("rangeStart").toDateTime();
    QMetaObject::invokeMethod(m_window, "step", Q_ARG(QVariant, 1));
    QTRY_VERIFY(m_window->property("rangeStart").toDateTime() > range);
    QMetaObject::invokeMethod(m_window, "step", Q_ARG(QVariant, -1));
    QTRY_COMPARE(m_window->property("rangeStart").toDateTime(), range);
}

void UiTest::eventsShowInEveryView()
{
    const QDate today = QDate::currentDate();
    QVariantMap timed = m_app->newEventData(QDateTime(today, QTime(10, 0)), QDateTime(today, QTime(11, 30)), false);
    timed.insert(QStringLiteral("title"), QStringLiteral("Planning"));
    QVERIFY(m_app->saveEvent(timed, 2));
    QVariantMap allDay = m_app->newEventData(QDateTime(today, QTime(0, 0)), QDateTime(today.addDays(1), QTime(0, 0)), true);
    allDay.insert(QStringLiteral("title"), QStringLiteral("Trip"));
    QVERIFY(m_app->saveEvent(allDay, 2));

    m_app->setView(QStringLiteral("month"));
    QVERIFY(load());
    QTRY_COMPARE(findItems(m_window->contentItem(), QStringLiteral("eventLine")).size(), 2);

    m_app->setView(QStringLiteral("week"));
    QTRY_COMPARE(findItems(m_window->contentItem(), QStringLiteral("eventBlock")).size(), 1);
    QCOMPARE(findItems(m_window->contentItem(), QStringLiteral("eventLine")).size(), 1);

    m_app->setView(QStringLiteral("day"));
    QTRY_COMPARE(findItems(m_window->contentItem(), QStringLiteral("eventBlock")).size(), 1);
}

void UiTest::editorCreatesEvent()
{
    QVERIFY(load());
    const QDateTime start(QDate::currentDate(), QTime(14, 0));
    QMetaObject::invokeMethod(m_window, "newEvent", Q_ARG(QVariant, start), Q_ARG(QVariant, start.addSecs(3600)),
                              Q_ARG(QVariant, false));
    QObject *editor = m_window->property("editor").value<QObject *>();
    QVERIFY(editor);
    auto *editorWindow = qobject_cast<QQuickWindow *>(editor);
    QVERIFY(editorWindow);
    QVERIFY(QTest::qWaitForWindowExposed(editorWindow));
    const QList<QQuickItem *> titles = findItems(editorWindow->contentItem(), QStringLiteral("titleField"));
    QCOMPARE(titles.size(), 1);
    titles.first()->setProperty("text", QStringLiteral("Dentist"));
    QMetaObject::invokeMethod(editor, "save");
    QTRY_VERIFY(!m_window->property("editor").value<QObject *>());

    const QList<Occurrence> today = m_store->occurrences(QDate::currentDate(), QDate::currentDate().addDays(1));
    QCOMPARE(today.size(), 1);
    QCOMPARE(today.first().summary, QStringLiteral("Dentist"));
    QCOMPARE(today.first().start, start);
}

void UiTest::searchFindsEvent()
{
    QVariantMap event = m_app->newEventData(QDateTime(QDate::currentDate().addDays(3), QTime(9, 0)),
                                            QDateTime(QDate::currentDate().addDays(3), QTime(10, 0)), false);
    event.insert(QStringLiteral("title"), QStringLiteral("Piano lesson"));
    QVERIFY(m_app->saveEvent(event, 2));
    QVERIFY(load());

    const QList<QQuickItem *> fields = findItems(m_window->contentItem(), QStringLiteral("searchField"));
    QCOMPARE(fields.size(), 1);
    fields.first()->setProperty("text", QStringLiteral("piano"));
    QTRY_COMPARE(findItems(m_window->contentItem(), QStringLiteral("searchResults")).size(), 1);
    QQuickItem *panel = findItems(m_window->contentItem(), QStringLiteral("searchResults")).first();
    QCOMPARE(panel->property("results").toList().size(), 1);
}

void UiTest::sidebarFollowsWidth()
{
    QVERIFY(load());
    QObject *sidebar = m_window->findChild<QObject *>(QStringLiteral("sidebar"));
    QVERIFY(sidebar);
    QTRY_VERIFY(sidebar->property("visible").toBool());

    QMetaObject::invokeMethod(m_window, "toggleSidebar");
    QTRY_VERIFY(!sidebar->property("visible").toBool());
    QVERIFY(!m_app->sidebarVisible());
    QMetaObject::invokeMethod(m_window, "toggleSidebar");
    QTRY_VERIFY(sidebar->property("visible").toBool());

    // A narrow window hides it, shows it over the calendar on demand, and
    // gives it back when wide again.
    m_window->resize(780, 640);
    QTRY_VERIFY(!sidebar->property("visible").toBool());
    QVERIFY(m_app->sidebarVisible());
    QMetaObject::invokeMethod(m_window, "toggleSidebar");
    QTRY_VERIFY(sidebar->property("visible").toBool());
    QVERIFY(m_window->property("sidebarOverlay").toBool());
    m_window->resize(1100, 740);
    QTRY_COMPARE(m_window->property("dockReveal").toReal(), 1.0);
    QVERIFY(!m_window->property("sidebarOverlay").toBool());
}

QTEST_MAIN(UiTest)

#include "tst_ui.moc"
