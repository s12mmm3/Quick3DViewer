#ifndef APP_H
#define APP_H

#include "definevaluehelper.h"

#include <QObject>
#include <QQmlEngine>
#include <QTranslator>

namespace UINamespace {

/**
 * @class App
 * @brief App单例，管理应用相关内容
 */
class App : public QObject
{
    Q_OBJECT
    DEFINE_VALUE(QQmlEngine*, qmlEngine, Q_NULLPTR);
public:
    static App* instance();

public:
    Q_INVOKABLE QStringList availableLanguages();

    Q_INVOKABLE void switchLanguage(const QString &lang);

    Q_INVOKABLE void setSystemLanguage();

private:
    QTranslator m_translator;
};
}

#endif // APP_H
