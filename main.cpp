#include "meshloader.h"
#include "dumpcatcher.h"
#include "globalconst.h"
#include "manager/logmgr.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QOperatingSystemVersion>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QFontDatabase>
#include <QFont>

using namespace UINamespace;

using namespace Qt::StringLiterals;

static constexpr auto styleKey = "style"_L1;

void registerTypes()
{
    qmlRegisterType<MeshLoader>("ModelLoader", 1, 0, "MeshLoader");
}

bool contextPropertys(QQmlEngine& engine)
{
    engine.rootContext()->setContextProperty("$logmgr", LogMgr::instance());
    return true;
}

int main(int argc, char *argv[])
{
    DumpCatcher::initDumpCatcher(APPLICATION_NAME);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
    QCoreApplication::setApplicationName(APPLICATION_NAME);
    QCoreApplication::setOrganizationName(ORGANIZATION_NAME);

    QGuiApplication app(argc, argv);
#if defined(Q_OS_WASM) || defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // 加载字体
    int fontId = QFontDatabase::addApplicationFont(":/fonts/HarmonyOS_Sans_SC_Medium.ttf");
    QString family = QFontDatabase::applicationFontFamilies(fontId).value(0);
    if (!family.isEmpty())
    {
        // 设置全局字体
        QFont font(family);
        app.setFont(font);
    }
#else
#endif

    registerTypes();

    QQmlApplicationEngine engine;
    contextPropertys(engine);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return -1;

    return QCoreApplication::exec();
}
