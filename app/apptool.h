#ifndef APPTOOL_H
#define APPTOOL_H

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QUrl>

namespace UINamespace {

/**
 * @class AppTool
 * @brief AppTool工具类，封装一些通用的方法，直接由QML调用
 */
class AppTool : public QObject
{
    Q_OBJECT
public:
    static AppTool* instance();

public:

    // 将文件url转化为文件路径
    Q_INVOKABLE QString urlToLocalFile(const QUrl& url);

    // 将文件路径转化为文件url
    Q_INVOKABLE QUrl localFileToUrl(const QString& path);

    Q_INVOKABLE bool saveText(const QString& fileName, const QVariantList& content);

    Q_INVOKABLE QString getText(const QString& fileName);

    Q_INVOKABLE QString compilerString();
    Q_INVOKABLE qint64 buildtime();
    Q_INVOKABLE int maxThreadCount();
    Q_INVOKABLE QString qVersion();
    Q_INVOKABLE QVariantMap getSysInfo();

    Q_INVOKABLE QVariantList collectModelFiles(const QVariantList &sources);
    Q_INVOKABLE bool isSupportedModelFile(const QUrl &url) const;
};
}

#endif // APPTOOL_H
