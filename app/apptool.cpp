#include "apptool.h"
#include "logger.h"
#include "globalconst.h"

#include <QVariantMap>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <QtConcurrent>
#include <QLibraryInfo>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QTemporaryFile>
#include <QVariant>

using namespace UINamespace;

namespace {

const QSet<QString> &supportedModelSuffixes()
{
    static const QSet<QString> suffixes = {
        QStringLiteral("ply"),
        QStringLiteral("stl"),
        QStringLiteral("obj"),
        QStringLiteral("gltf"),
        QStringLiteral("glb")
    };
    return suffixes;
}

QString suffixFromFileName(const QString &fileName)
{
    return QFileInfo(fileName).suffix().toLower();
}

bool isSupportedFileInfo(const QFileInfo &info)
{
    return info.exists() && info.isFile() &&
           supportedModelSuffixes().contains(info.suffix().toLower());
}

bool isSupportedSuffix(const QString &suffix)
{
    return supportedModelSuffixes().contains(suffix.toLower());
}

QUrl toLocalFileUrl(const QString &path)
{
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

} // namespace

Q_GLOBAL_STATIC(AppTool, appTool)
AppTool *AppTool::instance()
{
    return appTool();
}

QString AppTool::urlToLocalFile(const QUrl &url)
{
    return url.toLocalFile();
}

QUrl AppTool::localFileToUrl(const QString &path)
{
    return QUrl::fromLocalFile(path);
}

bool AppTool::saveText(const QString &fileName, const QVariantList &content)
{
    QUrl url(fileName);
    QFile file(url.isLocalFile() ? urlToLocalFile(fileName) : url.toString());

    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        WARNING << file.errorString();
        return false;
    }
    file.write("\xEF\xBB\xBF");
    QString strData;
    auto addRow = [&](const QStringList& row) {
        strData += (R"(")" + row.join(R"(",")") + "\"\n");
    };
    for (auto i: content) addRow(i.toStringList());
    file.write(strData.toUtf8());
    file.close(); // 确保关闭文件

    return true;
}

QString AppTool::getText(const QString &fileName)
{
    QUrl url(fileName);
    QFile file(url.isValid()
                   ? url.isLocalFile() ? urlToLocalFile(fileName) : url.toString()
                   : fileName);

    if (!file.open(QFile::ReadOnly)) {
        WARNING << file.errorString() << file.fileName();
        return "";
    }

    return file.readAll();
}

QString AppTool::compilerString()
{
#if defined(Q_CC_CLANG) // must be before GNU, because clang claims to be GNU too
    QString platformSpecific;
#if defined(__apple_build_version__) // Apple clang has other version numbers
    platformSpecific = QLatin1String(" (Apple)");
#elif defined(Q_CC_MSVC)
    platformSpecific = QLatin1String(" (clang-cl)");
#endif
    return QLatin1String("Clang ") + QString::number(__clang_major__) + QLatin1Char('.')
           + QString::number(__clang_minor__) + platformSpecific;
#elif defined(Q_CC_GNU)
    return QLatin1String("GCC ") + QLatin1String(__VERSION__);
#elif defined(Q_CC_MSVC)
    if (_MSC_VER > 1999)
        return QLatin1String("MSVC <unknown>");
    if (_MSC_VER >= 1930)
        return QLatin1String("MSVC 2022");
    if (_MSC_VER >= 1920)
        return QLatin1String("MSVC 2019");
    if (_MSC_VER >= 1910)
        return QLatin1String("MSVC 2017");
    if (_MSC_VER >= 1900)
        return QLatin1String("MSVC 2015");
#endif
    return QLatin1String("<unknown compiler>");
}

qint64 AppTool::buildtime()
{
    auto raw = QString("%1 %2").arg(__DATE__).arg(__TIME__).replace("  ", " 0").simplified();
    return QDateTime::fromString(raw, QStringLiteral("MMM dd yyyy hh:mm:ss")).toMSecsSinceEpoch();
}

int AppTool::maxThreadCount()
{
    return QThreadPool::globalInstance()->maxThreadCount();
}

QString AppTool::qVersion()
{
    return QLibraryInfo::version().toString();
}

QVariantMap AppTool::getSysInfo()
{
    return {
        { "bootUniqueId()",    QSysInfo::bootUniqueId() },
        { "buildAbi()",    QSysInfo::buildAbi() },
        { "buildCpuArchitecture()",	QSysInfo::buildCpuArchitecture() },
        { "currentCpuArchitecture()",	QSysInfo::currentCpuArchitecture() },
        { "kernelType()",	QSysInfo::kernelType() },
        { "kernelVersion()",	QSysInfo::kernelVersion() },
        { "machineHostName()",	QSysInfo::machineHostName() },
        { "machineUniqueId()",	QSysInfo::machineUniqueId() },
        { "prettyProductName()",	QSysInfo::prettyProductName() },
        { "productType()",	QSysInfo::productType() },
        { "productVersion()",	QSysInfo::productVersion() },
        };
}

QVariantList AppTool::collectModelFiles(const QVariantList &sources)
{
    QVariantList result;
    if (sources.isEmpty())
        return result;

    auto appendFromLocalPath = [&](const QString &path) {
        if (path.isEmpty())
            return;
        QFileInfo info(path);
        if (!info.exists())
            return;
        if (info.isDir()) {
            QDirIterator it(info.absoluteFilePath(),
                            QStringList(),
                            QDir::Files | QDir::NoSymLinks | QDir::Readable,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo childInfo = it.fileInfo();
                if (!isSupportedFileInfo(childInfo))
                    continue;
                result.append(toLocalFileUrl(childInfo.absoluteFilePath()));
            }
            return;
        }
        if (isSupportedFileInfo(info))
            result.append(toLocalFileUrl(info.absoluteFilePath()));
    };

    auto appendNonLocal = [&](const QUrl &url) {
        if (!url.isValid())
            return;
        const QString fileName = url.fileName();
        const QString suffix = suffixFromFileName(fileName.isEmpty() ? url.path() : fileName);
        if (!isSupportedSuffix(suffix))
            return;
        result.append(url);
    };

    for (const QVariant &value : sources) {
        QUrl url;
        if (value.canConvert<QUrl>())
            url = value.toUrl();
        else if (value.canConvert<QString>())
            url = QUrl(value.toString());
        else
            continue;

        if (!url.isValid())
            continue;

        if (url.isLocalFile() || url.scheme().isEmpty()) {
            const QString localPath = url.isLocalFile() ? url.toLocalFile()
                                                        : QUrl::fromUserInput(url.toString()).toLocalFile();
            appendFromLocalPath(localPath.isEmpty() ? url.toString() : localPath);
        } else {
            appendNonLocal(url);
        }
    }

    return result;
}

bool AppTool::isSupportedModelFile(const QUrl &url) const
{
    if (!url.isValid())
        return false;
    if (url.isLocalFile() || url.scheme().isEmpty()) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        return isSupportedFileInfo(QFileInfo(path));
    }
    return isSupportedFileInfo(QFileInfo(url.path()));
}

QString AppTool::fileNameFromSource(const QVariant &source) const
{
    QUrl url;
    if (source.canConvert<QUrl>())
        url = source.toUrl();
    else if (source.canConvert<QString>())
        url = QUrl(source.toString());

    if (url.isValid()) {
        if (url.isLocalFile())
            return QFileInfo(url.toLocalFile()).fileName();

        QString name = url.fileName();
        if (name.isEmpty())
            name = QFileInfo(url.path()).fileName();
        if (!name.isEmpty())
            return name;
    }

    const QString fallback = source.toString();
    const QString baseName = QFileInfo(fallback).fileName();
    if (!baseName.isEmpty())
        return baseName;

    return fallback;
}
