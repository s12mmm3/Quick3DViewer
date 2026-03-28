#include "meshloader.h"

#include "logger.h"
#include "meshimporthelper.h"

#include <QByteArray>
#include <QFileInfo>
#include <QtMath>

#include <limits>
#include <utility>

namespace {
struct VertexData
{
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float tu = 0.0f;
    float tv = 0.0f;
};
} // namespace

MeshLoader::MeshLoader(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    resetGeometry();
    connect(this, &MeshLoader::sourceChanged, this, &MeshLoader::handleSourceChanged);
}

void MeshLoader::setSource(const QUrl &source)
{
    set_source(source);
}

void MeshLoader::handleSourceChanged()
{
    resetGeometry();

    const QUrl current = source();
    if (current.isEmpty())
        return;

    const QString filePath = current.isLocalFile()
                                 ? current.toLocalFile()
                                 : current.toString(QUrl::FullyDecoded);
    if (!loadFile(filePath))
        WARNING << "MeshLoader: failed to load" << filePath << errorString();
}

void MeshLoader::resetGeometry()
{
    clear();
    setBounds(QVector3D(), QVector3D());
    set_boundsMin(QVector3D());
    set_boundsMax(QVector3D());
    set_boundsCenter(QVector3D());
    set_boundingRadius(0.0f);
    set_hasData(false);
    set_colorTexture(QUrl());
    emit boundsChanged();
    set_errorString(QString());
}

bool MeshLoader::loadFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    DEBUG << "MeshLoader loading" << filePath << "as" << suffix;

    MeshImportResult importResult;
    QString errorMessage;
    if (!MeshImportHelper::load(filePath, importResult, &errorMessage)) {
        if (errorMessage.isEmpty())
            errorMessage = QStringLiteral("Failed to load mesh: %1").arg(filePath);
        set_errorString(errorMessage);
        WARNING << "MeshLoader load failed" << filePath << errorMessage;
        return false;
    }

    set_errorString(QString());
    set_colorTexture(importResult.texture);
    uploadMesh(importResult.positions,
               importResult.normals,
               importResult.indices,
               importResult.texCoords);
    DEBUG << "MeshLoader load success" << filePath;
    return true;
}

void MeshLoader::uploadMesh(const QVector<QVector3D> &positions,
                            QVector<QVector3D> normals,
                            const QVector<unsigned int> &indices,
                            const QVector<QVector2D> &texCoords)
{
    if (positions.isEmpty() || indices.size() < 3) {
        set_errorString(QStringLiteral("Mesh contains no triangles"));
        return;
    }

    const QVector<unsigned int> cleanedIndices = sanitizeIndices(indices, positions);
    if (cleanedIndices.size() < 3) {
        set_errorString(QStringLiteral("Mesh contains no valid triangles"));
        return;
    }

    const bool sanitized = cleanedIndices.size() != indices.size();
    const QVector<unsigned int> &finalIndices = cleanedIndices;

    if (normals.size() != positions.size())
        normals.resize(positions.size());

    bool needNormals = false;
    for (const auto &n : std::as_const(normals)) {
        if (n.isNull()) {
            needNormals = true;
            break;
        }
    }

    if (needNormals)
        computeNormals(normals, finalIndices, positions);

    QVector<QVector2D> fullTexCoords = texCoords;
    if (fullTexCoords.size() != positions.size())
        fullTexCoords.resize(positions.size());
    DEBUG << "uploadMesh positions" << positions.size()
             << "indices" << finalIndices.size()
             << "texcoords" << fullTexCoords.size();
    if (sanitized)
        DEBUG << "sanitize removed" << (indices.size() - finalIndices.size()) / 3 << "degenerate triangles";

    QVector3D minPoint(std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max());
    QVector3D maxPoint(std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest());
    for (const auto &pos : positions) {
        minPoint.setX(qMin(minPoint.x(), pos.x()));
        minPoint.setY(qMin(minPoint.y(), pos.y()));
        minPoint.setZ(qMin(minPoint.z(), pos.z()));
        maxPoint.setX(qMax(maxPoint.x(), pos.x()));
        maxPoint.setY(qMax(maxPoint.y(), pos.y()));
        maxPoint.setZ(qMax(maxPoint.z(), pos.z()));
    }

    QByteArray vertexBuffer;
    vertexBuffer.resize(positions.size() * sizeof(VertexData));
    auto *vertexData = reinterpret_cast<VertexData *>(vertexBuffer.data());
    for (int i = 0; i < positions.size(); ++i) {
        const QVector3D pos = positions.at(i);
        const QVector3D n = normals.at(i).isNull() ? QVector3D(0, 1, 0) : normals.at(i).normalized();
        const QVector2D tex = i < fullTexCoords.size() ? fullTexCoords.at(i) : QVector2D();
        vertexData[i].px = pos.x();
        vertexData[i].py = pos.y();
        vertexData[i].pz = pos.z();
        vertexData[i].nx = n.x();
        vertexData[i].ny = n.y();
        vertexData[i].nz = n.z();
        vertexData[i].tu = tex.x();
        vertexData[i].tv = tex.y();
    }

    QByteArray indexBuffer;
    indexBuffer.resize(finalIndices.size() * sizeof(quint32));
    auto *indexData = reinterpret_cast<quint32 *>(indexBuffer.data());
    for (int i = 0; i < finalIndices.size(); ++i)
        indexData[i] = finalIndices.at(i);

    set_boundsMin(minPoint);
    set_boundsMax(maxPoint);
    set_boundsCenter(computeBoundsCenter(minPoint, maxPoint));
    set_boundingRadius((maxPoint - minPoint).length() * 0.5f);
    DEBUG << "bounds" << boundsMin() << boundsMax()
             << "center" << boundsCenter()
             << "radius" << boundingRadius();

    clear();
    setVertexData(vertexBuffer);
    setIndexData(indexBuffer);
    setStride(sizeof(VertexData));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 offsetof(VertexData, px),
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic,
                 offsetof(VertexData, nx),
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 offsetof(VertexData, tu),
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0,
                 QQuick3DGeometry::Attribute::U32Type);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setBounds(boundsMin(), boundsMax());
    markAllDirty();

    set_hasData(true);
    emit boundsChanged();
    DEBUG << "mesh bounds" << boundsMin() << boundsMax() << finalIndices.size() / 3 << "triangles";
}

void MeshLoader::computeNormals(QVector<QVector3D> &normals,
                                const QVector<unsigned int> &indices,
                                const QVector<QVector3D> &positions)
{
    normals.fill(QVector3D());
    for (int i = 0; i + 2 < indices.size(); i += 3) {
        const unsigned int ia = indices.at(i);
        const unsigned int ib = indices.at(i + 1);
        const unsigned int ic = indices.at(i + 2);
        if (ia >= static_cast<unsigned int>(positions.size()) ||
            ib >= static_cast<unsigned int>(positions.size()) ||
            ic >= static_cast<unsigned int>(positions.size()))
            continue;
        const QVector3D &a = positions.at(ia);
        const QVector3D &b = positions.at(ib);
        const QVector3D &c = positions.at(ic);
        QVector3D normal = QVector3D::crossProduct(b - a, c - a);
        if (!normal.isNull())
            normal.normalize();
        normals[ia] += normal;
        normals[ib] += normal;
        normals[ic] += normal;
    }

    for (auto &normal : normals) {
        if (!normal.isNull())
            normal.normalize();
        else
            normal = QVector3D(0, 1, 0);
    }
}

QVector3D MeshLoader::computeBoundsCenter(const QVector3D &minBounds, const QVector3D &maxBounds)
{
    return (minBounds + maxBounds) * 0.5f;
}

QVector<unsigned int> MeshLoader::sanitizeIndices(const QVector<unsigned int> &indices,
                                                  const QVector<QVector3D> &positions) const
{
    QVector<unsigned int> cleaned;
    cleaned.reserve(indices.size());

    const int positionCount = positions.size();
    int removed = 0;
    int outOfRange = 0;
    int duplicate = 0;
    int flat = 0;

    for (int i = 0; i + 2 < indices.size(); i += 3) {
        const unsigned int ia = indices.at(i);
        const unsigned int ib = indices.at(i + 1);
        const unsigned int ic = indices.at(i + 2);

        if (ia >= static_cast<unsigned int>(positionCount) ||
            ib >= static_cast<unsigned int>(positionCount) ||
            ic >= static_cast<unsigned int>(positionCount)) {
            ++removed;
            ++outOfRange;
            continue;
        }

        if (ia == ib || ib == ic || ia == ic) {
            ++removed;
            ++duplicate;
            continue;
        }

        const QVector3D &a = positions.at(ia);
        const QVector3D &b = positions.at(ib);
        const QVector3D &c = positions.at(ic);

        const QVector3D ab = b - a;
        const QVector3D ac = c - a;
        const QVector3D cross = QVector3D::crossProduct(ab, ac);
        if (cross.lengthSquared() <= 1e-6f) {
            ++removed;
            ++flat;
            continue;
        }

        cleaned.append(ia);
        cleaned.append(ib);
        cleaned.append(ic);
    }

    if (removed > 0) {
        WARNING << "sanitizeIndices dropped" << removed << "triangles"
                   << "(out-of-range:" << outOfRange
                   << "duplicates:" << duplicate
                   << "flat/zero-area:" << flat << ")";
    }
    return cleaned;
}
