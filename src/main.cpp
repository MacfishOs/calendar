// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "calendarstore.h"
#include "cutefishapplication.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

int main(int argc, char *argv[])
{
    Cutefish::Application::prepare();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cutefish-calendar"));
    app.setApplicationVersion(QStringLiteral(CALENDAR_VERSION));
    app.setDesktopFileName(QStringLiteral("cutefish-calendar"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("office-calendar")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cutefish Calendar"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dateOption(QStringLiteral("date"), QStringLiteral("Show this day."),
                                        QStringLiteral("YYYY-MM-DD"));
    parser.addOption(dateOption);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Calendar files to import."),
                                 QStringLiteral("[FILE...]"));
    parser.process(app);

    QStringList files;
    const QStringList arguments = parser.positionalArguments();
    for (const QString &argument : arguments) {
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        files.append(url.isLocalFile() ? QFileInfo(url.toLocalFile()).absoluteFilePath() : argument);
    }
    const QString date = parser.value(dateOption);

    // A running calendar takes the files and the day, and this process ends.
    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("com.cutefish.Calendar");
    const QString path = QStringLiteral("/Calendar");
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface existing(service, path, service, bus);
        existing.setTimeout(5000);
        existing.call(QStringLiteral("open"), files, date);
        return 0;
    }

    CalendarStore store;
    Application application(&store);
    bus.registerObject(path, &application, QDBusConnection::ExportScriptableSlots);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &application);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&application, &Application::activationRequested, window, [window] {
        window->show();
        window->raise();
        window->requestActivate();
    });
    store.loadAsync();
    if (!files.isEmpty() || !date.isEmpty()) {
        // Imports wait for the calendars to be read.
        QObject::connect(&application, &Application::readyChanged, &application, [&application, files, date] {
            application.open(files, date);
        }, Qt::SingleShotConnection);
    }
    return app.exec();
}
