#pragma once

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

class MeshImportHelper
{
public:
    static bool load(const QString &filePath,
                     MeshImportResult &result,
                     QString *errorOut = nullptr);
};
