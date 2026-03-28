#ifndef MESHIMPORTERUTILS_H
#define MESHIMPORTERUTILS_H

#include <QDir>
#include <QList>
#include <QTextStream>
#include <QUrl>
#include <QVector3D>
#include <QStringList>

class MeshImportUtils
{
public:
    static void assignError(QString *errorOut, const QString &message);
    static QString normalizePath(const QString &path);
    static QVector3D parseVector3(const QStringList &tokens, int offset);
    static bool parseMaterialFile(const QString &materialPath,
                                  const QDir &baseDir,
                                  QUrl &textureUrl);
    static QUrl loadObjMaterials(const QString &geometryPath,
                                 const QStringList &materialLibs);
    static QUrl loadPlyMaterial(const QString &geometryPath);
};

#endif // MESHIMPORTERUTILS_H
