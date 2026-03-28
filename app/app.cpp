#include "app.h"
#include "globalconst.h"
#include "logger.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>

using namespace Qt::Literals::StringLiterals;

using namespace UINamespace;

Q_GLOBAL_STATIC(App, app)
App* App ::instance()
{
    return app();
}

QStringList App::availableLanguages()
{
    QString scanDir = ":/i18n/";
    QStringList result;
    for (const QFileInfo &fileInfo : QDir(scanDir).entryInfoList()) {
        QString name = fileInfo.baseName().remove(APPLICATION_NAME + "_");
        result.push_back(name);
    }
    return result;
}

void App::switchLanguage(const QString &lang)
{
    QLocale locale(lang);
    QLocale::setDefault(locale);
    qApp->removeTranslator(&m_translator); // not necessary from Qt 6.10
    if (m_translator.load(locale, APPLICATION_NAME, "_"_L1, ":/i18n/"_L1)
        && qApp->installTranslator(&m_translator)) {
        m_qmlEngine->retranslate();
        INFO.noquote() << "loaded language:" << locale.name() << locale.nativeLanguageName() << locale.nativeTerritoryName();
    } else {
        WARNING.noquote() << "Could not load translation to %s!" << qPrintable(locale.name());
    }
}

void App::setSystemLanguage()
{
    const auto availableLanguages = this->availableLanguages();
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString &localeName = QLocale(locale).name();
        if (availableLanguages.contains(localeName)) {
            switchLanguage(localeName);
            break;
        }
    }
}
