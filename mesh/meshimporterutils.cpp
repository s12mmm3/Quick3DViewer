#include "meshimporterutils.h"

#include "logger.h"

#include <QFile>
#include <QFileInfo>
#include <QStringConverter>

void MeshImportUtils::assignError(QString *errorOut, const QString &message)
{
    if (errorOut)
        *errorOut = message;
}

QVector3D MeshImportUtils::parseVector3(const QStringList &tokens, int offset)
{
    if (tokens.size() <= offset + 2)
        return QVector3D();
    bool okX = false;
    bool okY = false;
    bool okZ = false;
    const float x = tokens[offset].toFloat(&okX);
    const float y = tokens[offset + 1].toFloat(&okY);
    const float z = tokens[offset + 2].toFloat(&okZ);
    if (!okX || !okY || !okZ)
        return QVector3D();
    return {x, y, z};
}

QString MeshImportUtils::normalizePath(const QString &path)
{
    QString normalized = path;
    normalized.replace('\\', '/');
    normalized = QDir::cleanPath(normalized);
    return normalized;
}

bool MeshImportUtils::parseMaterialFile(const QString &materialPath,
                                        const QDir &baseDir,
                                        QUrl &textureUrl)
{
    QFile materialFile(materialPath);
    if (!materialFile.exists())
        return false;
    if (!materialFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        WARNING << "MeshLoader: unable to open material file" << materialPath;
        return false;
    }

    QTextStream stream(&materialFile);
    stream.setEncoding(QStringConverter::Utf8);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QStringLiteral("map_Kd"), Qt::CaseInsensitive)) {
            QString texturePath = line.mid(6).trimmed();
            if (texturePath.startsWith(QLatin1Char('"')) && texturePath.endsWith(QLatin1Char('"')) && texturePath.size() >= 2) {
                texturePath = texturePath.mid(1, texturePath.size() - 2);
            } else {
                const QStringList parts = texturePath.split(' ', Qt::SkipEmptyParts);
                for (int i = parts.size() - 1; i >= 0; --i) {
                    if (!parts.at(i).startsWith('-')) {
                        texturePath = parts.at(i);
                        break;
                    }
                }
            }

            if (texturePath.isEmpty())
                continue;

            texturePath = MeshImportUtils::normalizePath(texturePath);

            QString resolvedPath = texturePath;
            QFileInfo texInfo(resolvedPath);
            if (!texInfo.isAbsolute()) {
                resolvedPath = baseDir.absoluteFilePath(texturePath);
                resolvedPath = MeshImportUtils::normalizePath(resolvedPath);
                texInfo.setFile(resolvedPath);
            }

            if (texInfo.exists())
                textureUrl = QUrl::fromLocalFile(resolvedPath);
            else
                textureUrl = QUrl::fromUserInput(texturePath);
            return true;
        }
    }

    return false;
}

QUrl MeshImportUtils::loadObjMaterials(const QString &geometryPath,
                                       const QStringList &materialLibs)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists() || materialLibs.isEmpty())
        return QUrl();

    for (const QString &lib : materialLibs) {
        QString trimmed = lib.trimmed();
        if (trimmed.isEmpty())
            continue;
        trimmed = MeshImportUtils::normalizePath(trimmed);

        QString materialPath = trimmed;
        QFileInfo libInfo(materialPath);
        if (!libInfo.isAbsolute()) {
            materialPath = geometryInfo.dir().absoluteFilePath(materialPath);
            materialPath = MeshImportUtils::normalizePath(materialPath);
        }
        QUrl texture;
        if (MeshImportUtils::parseMaterialFile(materialPath, geometryInfo.dir(), texture))
            return texture;
    }

    return QUrl();
}

QUrl MeshImportUtils::loadPlyMaterial(const QString &geometryPath)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists())
        return QUrl();

    const QString materialPath = MeshImportUtils::normalizePath(
        geometryInfo.dir().absoluteFilePath(geometryInfo.completeBaseName() + QStringLiteral(".mtl")));
    QUrl texture;
    if (MeshImportUtils::parseMaterialFile(materialPath, geometryInfo.dir(), texture))
        return texture;
    return QUrl();
}
