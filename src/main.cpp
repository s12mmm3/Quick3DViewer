#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QLoggingCategory>
#include <QtCore/qstringliteral.h>

#include "meshloader.h"

int main(int argc, char *argv[])
{
    QLoggingCategory::setFilterRules("qt.quick3d.render.warning=true");
    QGuiApplication::setApplicationName("Q3DViewer");
    QGuiApplication::setOrganizationName("CodexSamples");

    QGuiApplication app(argc, argv);

    qmlRegisterType<MeshLoader>("ModelLoader", 1, 0, "MeshLoader");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
