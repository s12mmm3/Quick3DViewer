#pragma once

#include <QDir>
#include <QFile>
#include <QQuick3DGeometry>
#include <QObject>
#include <QQuick3DObject>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

class MeshLoader : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QUrl colorTexture READ colorTexture NOTIFY colorTextureChanged)
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsCenter READ boundsCenter NOTIFY boundsChanged)
    Q_PROPERTY(float boundingRadius READ boundingRadius NOTIFY boundsChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY hasDataChanged)

public:
    explicit MeshLoader(QQuick3DObject *parent = nullptr);

    QUrl source() const;
    void setSource(const QUrl &source);

    QString errorString() const;

    QUrl colorTexture() const;

    QVector3D boundsMin() const;
    QVector3D boundsMax() const;
    QVector3D boundsCenter() const;
    float boundingRadius() const;
    bool hasData() const;

signals:
    void sourceChanged();
    void errorChanged();
    void colorTextureChanged();
    void boundsChanged();
    void hasDataChanged();

private:
    void resetGeometry();
    bool loadFile(const QString &filePath);
    bool loadObj(const QString &filePath);
    bool loadStl(const QString &filePath);
    bool loadStlAscii(QFile &file);
    bool loadStlBinary(QFile &file);
    bool loadPly(const QString &filePath);
    void loadPlyMaterial(const QString &geometryPath);
    void loadObjMaterials(const QString &geometryPath, const QStringList &materialLibs);
    bool parseMaterialFile(const QString &materialPath, const QDir &baseDir);

    void uploadMesh(const QVector<QVector3D> &positions,
                    QVector<QVector3D> normals,
                    const QVector<unsigned int> &indices,
                    const QVector<QVector2D> &texCoords = {});

    void computeNormals(QVector<QVector3D> &normals,
                        const QVector<unsigned int> &indices,
                        const QVector<QVector3D> &positions);

    static QVector3D computeBoundsCenter(const QVector3D &minBounds, const QVector3D &maxBounds);

    void setError(const QString &message);
    void setColorTexture(const QUrl &textureUrl);
    QVector<unsigned int> sanitizeIndices(const QVector<unsigned int> &indices,
                                          const QVector<QVector3D> &positions) const;

    QString m_error;
    QUrl m_source;
    QUrl m_colorTexture;
    QVector3D m_boundsMin;
    QVector3D m_boundsMax;
    QVector3D m_boundsCenter;
    float m_boundingRadius = 0.0f;
    bool m_hasData = false;
};
