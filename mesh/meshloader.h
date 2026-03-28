#ifndef MESHLOADER_H
#define MESHLOADER_H

#include "definevaluehelper.h"

#include <QQuick3DGeometry>
#include <QQuick3DObject>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QUrl>

class MeshLoader : public QQuick3DGeometry
{
    Q_OBJECT
    DEFINE_VALUE(QUrl, source, QUrl())
    DEFINE_VALUE(QString, errorString, QString())
    DEFINE_VALUE(QUrl, colorTexture, QUrl())
    DEFINE_VALUE(QVector3D, boundsMin, QVector3D())
    DEFINE_VALUE(QVector3D, boundsMax, QVector3D())
    DEFINE_VALUE(QVector3D, boundsCenter, QVector3D())
    DEFINE_VALUE(float, boundingRadius, 0.0f)
    DEFINE_VALUE(bool, hasData, false)

public:
    explicit MeshLoader(QQuick3DObject *parent = nullptr);

    void setSource(const QUrl &source);

Q_SIGNALS:
    void boundsChanged();

private Q_SLOTS:
    void handleSourceChanged();

private:
    void resetGeometry();
    bool loadFile(const QString &filePath);

    void uploadMesh(const QVector<QVector3D> &positions,
                    QVector<QVector3D> normals,
                    const QVector<unsigned int> &indices,
                    const QVector<QVector2D> &texCoords = {});

    void computeNormals(QVector<QVector3D> &normals,
                        const QVector<unsigned int> &indices,
                        const QVector<QVector3D> &positions);

    static QVector3D computeBoundsCenter(const QVector3D &minBounds, const QVector3D &maxBounds);
    QVector<unsigned int> sanitizeIndices(const QVector<unsigned int> &indices,
                                          const QVector<QVector3D> &positions) const;
};

#endif // MESHLOADER_H
