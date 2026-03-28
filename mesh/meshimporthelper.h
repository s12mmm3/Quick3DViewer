#ifndef MESHIMPORTHELPER_H
#define MESHIMPORTHELPER_H

#include <QUrl>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QString>

struct MeshImportResult
{
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;
    QVector<unsigned int> indices;
    QUrl texture;
};

class AbstractMeshImporter
{
public:
    virtual ~AbstractMeshImporter() = default;
    virtual bool canLoad(const QString &suffix) const = 0;
    virtual bool load(const QString &filePath,
                      MeshImportResult &result,
                      QString *errorOut) const = 0;
};

class MeshImportHelper
{
public:
    static bool load(const QString &filePath,
                     MeshImportResult &result,
                     QString *errorOut = nullptr);
};

#endif // MESHIMPORTHELPER_H
