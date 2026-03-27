#include "meshloader.h"

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QStringConverter>
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

struct VertexKey
{
    int positionIndex = -1;
    int texCoordIndex = -1;
    int normalIndex = -1;

    bool operator==(const VertexKey &other) const noexcept
    {
        return positionIndex == other.positionIndex &&
               texCoordIndex == other.texCoordIndex &&
               normalIndex == other.normalIndex;
    }
};

inline uint qHash(const VertexKey &key, uint seed = 0) noexcept
{
    seed ^= ::qHash(key.positionIndex, seed ^ 0x9e3779b9);
    seed ^= ::qHash(key.texCoordIndex, seed << 1);
    seed ^= ::qHash(key.normalIndex, seed << 2);
    return seed;
}
} // namespace

MeshLoader::MeshLoader(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    resetGeometry();
}

QUrl MeshLoader::source() const
{
    return m_source;
}

void MeshLoader::setSource(const QUrl &source)
{
    if (m_source == source)
        return;

    m_source = source;
    emit sourceChanged();

    resetGeometry();

    if (source.isEmpty())
        return;

    const QString filePath = source.isLocalFile() ? source.toLocalFile()
                                                  : source.toString(QUrl::FullyDecoded);
    if (!loadFile(filePath))
        qWarning() << "MeshLoader: failed to load" << filePath << m_error;
}

QString MeshLoader::errorString() const
{
    return m_error;
}

QVector3D MeshLoader::boundsMin() const
{
    return m_boundsMin;
}

QVector3D MeshLoader::boundsMax() const
{
    return m_boundsMax;
}

QVector3D MeshLoader::boundsCenter() const
{
    return m_boundsCenter;
}

float MeshLoader::boundingRadius() const
{
    return m_boundingRadius;
}

bool MeshLoader::hasData() const
{
    return m_hasData;
}

void MeshLoader::resetGeometry()
{
    clear();
    setBounds(QVector3D(), QVector3D());
    m_boundsMin = QVector3D();
    m_boundsMax = QVector3D();
    m_boundsCenter = QVector3D();
    m_boundingRadius = 0.0f;
    const bool previousHasData = m_hasData;
    m_hasData = false;
    if (previousHasData)
        emit hasDataChanged();
    emit boundsChanged();
    setError(QString());
}

bool MeshLoader::loadFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    bool ok = false;

    qDebug() << "MeshLoader loading" << filePath << "as" << suffix;
    if (suffix == QLatin1String("obj")) {
        ok = loadObj(filePath);
    } else if (suffix == QLatin1String("stl")) {
        ok = loadStl(filePath);
    } else if (suffix == QLatin1String("ply")) {
        ok = loadPly(filePath);
    } else {
        setError(QStringLiteral("Unsupported extension: %1").arg(suffix));
        return false;
    }

    if (ok) {
        setError(QString());
        qDebug() << "MeshLoader load success" << filePath;
    } else {
        qWarning() << "MeshLoader load failed" << filePath << m_error;
    }

    return ok;
}

static QVector3D parseVector3(const QStringList &tokens, int offset)
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

bool MeshLoader::loadObj(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    QVector<QVector3D> positions;
    QVector<QVector3D> normalsSource;
    QVector<QVector2D> texCoordsSource;

    QVector<QVector3D> finalPositions;
    QVector<QVector3D> finalNormals;
    QVector<QVector2D> finalTexCoords;
    QVector<unsigned int> indices;

    QHash<VertexKey, unsigned int> vertexMap;

    auto parseIndex = [](const QString &token, int count) -> int {
        if (token.isEmpty())
            return -1;
        bool ok = false;
        int idx = token.toInt(&ok);
        if (!ok || idx == 0)
            return -1;
        if (idx < 0)
            idx = count + idx + 1;
        return idx - 1;
    };

    auto acquireVertexIndex = [&](const QString &token) -> int {
        const QStringList parts = token.split('/', Qt::KeepEmptyParts);
        const int vIndex = parseIndex(parts.value(0), positions.size());
        if (vIndex < 0 || vIndex >= positions.size())
            return -1;
        const int tIndex = parseIndex(parts.value(1), texCoordsSource.size());
        const int nIndex = parseIndex(parts.value(2), normalsSource.size());

        const VertexKey key{vIndex, tIndex, nIndex};
        const auto it = vertexMap.constFind(key);
        if (it != vertexMap.cend())
            return it.value();

        finalPositions.append(positions.at(vIndex));
        finalTexCoords.append(tIndex >= 0 ? texCoordsSource.at(tIndex) : QVector2D());
        finalNormals.append(nIndex >= 0 ? normalsSource.at(nIndex) : QVector3D());

        const int nextIndex = finalPositions.size() - 1;
        vertexMap.insert(key, nextIndex);
        return nextIndex;
    };

    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;

        const QString &type = tokens.first();
        if (type == QLatin1String("v")) {
            if (tokens.size() < 4)
                continue;
            positions.append(parseVector3(tokens, 1));
        } else if (type == QLatin1String("vn")) {
            if (tokens.size() < 4)
                continue;
            normalsSource.append(parseVector3(tokens, 1));
        } else if (type == QLatin1String("vt")) {
            if (tokens.size() < 3)
                continue;
            bool okU = false;
            bool okV = false;
            const float u = tokens[1].toFloat(&okU);
            const float v = tokens[2].toFloat(&okV);
            texCoordsSource.append(okU && okV ? QVector2D(u, v) : QVector2D());
        } else if (type == QLatin1String("f")) {
            if (tokens.size() < 4)
                continue;
            QVector<unsigned int> faceIndices;
            faceIndices.reserve(tokens.size() - 1);
            for (int i = 1; i < tokens.size(); ++i) {
                const int index = acquireVertexIndex(tokens.at(i));
                if (index < 0)
                    continue;
                faceIndices.append(index);
            }
            for (int i = 1; i + 1 < faceIndices.size(); ++i) {
                indices.append(faceIndices.front());
                indices.append(faceIndices.at(i));
                indices.append(faceIndices.at(i + 1));
            }
        }
    }

    if (finalPositions.isEmpty() || indices.isEmpty()) {
        setError(QStringLiteral("OBJ file has no triangles"));
        return false;
    }

    uploadMesh(finalPositions, finalNormals, indices, finalTexCoords);
    return true;
}

bool MeshLoader::loadStl(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    const QByteArray header = file.peek(80);
    const bool asciiCandidate = header.trimmed().startsWith("solid");

    if (asciiCandidate) {
        file.seek(0);
        if (loadStlAscii(file))
            return true;
        file.seek(0);
    }

    file.seek(0);
    return loadStlBinary(file);
}

bool MeshLoader::loadStlAscii(QFile &file)
{
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    struct TempVertex
    {
        QVector3D position;
        QVector3D normal;
        QVector2D texCoord;
    };
    QVector<TempVertex> vertices;
    QVector<unsigned int> indices;

    QVector3D normal;
    QVector<QVector3D> tri;
    tri.reserve(3);

    auto flushTriangle = [&]() {
        if (tri.size() != 3)
            return;
        const QVector3D computedNormal = QVector3D::normal(tri[0], tri[1], tri[2]);
        const QVector3D faceNormal = normal.isNull() ? computedNormal : normal.normalized();
        for (int i = 0; i < 3; ++i) {
            TempVertex vd;
            vd.position = tri[i];
            vd.normal = faceNormal;
            vd.texCoord = QVector2D();
            vertices.append(vd);
            indices.append(vertices.size() - 1);
        }
        tri.clear();
        normal = QVector3D();
    };

    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QStringLiteral("facet normal"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            normal = parseVector3(tokens, 2);
        } else if (line.startsWith(QStringLiteral("vertex"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            tri.append(parseVector3(tokens, 1));
        } else if (line.startsWith(QStringLiteral("endfacet"))) {
            flushTriangle();
        }
    }

    if (vertices.isEmpty()) {
        setError(QStringLiteral("Failed to parse ASCII STL"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<unsigned int> finalIndices = indices;
    positions.reserve(vertices.size());
    normals.reserve(vertices.size());

    for (const auto &v : vertices) {
        positions.append(v.position);
        normals.append(v.normal);
    }

    uploadMesh(positions, normals, finalIndices);
    return true;
}

bool MeshLoader::loadStlBinary(QFile &file)
{
    if (file.size() < 84) {
        setError(QStringLiteral("STL file too small"));
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    file.seek(80);
    quint32 triangleCount = 0;
    in >> triangleCount;
    if (triangleCount == 0) {
        setError(QStringLiteral("Binary STL has zero triangles"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<unsigned int> indices;
    positions.reserve(triangleCount * 3);
    normals.reserve(triangleCount * 3);
    indices.reserve(triangleCount * 3);

    for (quint32 i = 0; i < triangleCount; ++i) {
        QVector3D normal;
        QVector3D verts[3];
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        in >> nx >> ny >> nz;
        normal = QVector3D(nx, ny, nz);
        for (int v = 0; v < 3; ++v) {
            float vx = 0.0f;
            float vy = 0.0f;
            float vz = 0.0f;
            in >> vx >> vy >> vz;
            verts[v] = QVector3D(vx, vy, vz);
        }
        quint16 attr = 0;
        in >> attr;
        const QVector3D computedNormal = QVector3D::normal(verts[0], verts[1], verts[2]);
        const QVector3D faceNormal = normal.isNull() ? computedNormal : normal.normalized();
        for (int v = 0; v < 3; ++v) {
            positions.append(verts[v]);
            normals.append(faceNormal);
            indices.append(positions.size() - 1);
        }
    }

    uploadMesh(positions, normals, indices);
    return true;
}

bool MeshLoader::loadPly(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    QString line = stream.readLine().trimmed();
    if (line != QLatin1String("ply")) {
        setError(QStringLiteral("Not a PLY file"));
        return false;
    }

    enum class PlyFormat {
        Unknown,
        Ascii
    };

    PlyFormat format = PlyFormat::Unknown;
    int vertexCount = 0;
    int faceCount = 0;
    QStringList vertexProperties;
    bool parsingVertexProperties = false;

    while (!stream.atEnd()) {
        line = stream.readLine().trimmed();
        if (line == QLatin1String("end_header"))
            break;
        if (line.startsWith(QStringLiteral("format"))) {
            if (line.contains(QStringLiteral("ascii")))
                format = PlyFormat::Ascii;
        } else if (line.startsWith(QStringLiteral("element"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            if (tokens.size() >= 3) {
                if (tokens[1] == QLatin1String("vertex")) {
                    vertexCount = tokens[2].toInt();
                    vertexProperties.clear();
                    parsingVertexProperties = true;
                } else {
                    parsingVertexProperties = false;
                    if (tokens[1] == QLatin1String("face"))
                        faceCount = tokens[2].toInt();
                }
            }
        } else if (line.startsWith(QStringLiteral("property")) && parsingVertexProperties) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            if (tokens.size() >= 3)
                vertexProperties.append(tokens.last());
        }
    }

    if (format != PlyFormat::Ascii) {
        setError(QStringLiteral("Only ASCII PLY is supported"));
        return false;
    }

    const int xIndex = vertexProperties.indexOf("x");
    const int yIndex = vertexProperties.indexOf("y");
    const int zIndex = vertexProperties.indexOf("z");
    const int nxIndex = vertexProperties.indexOf("nx");
    const int nyIndex = vertexProperties.indexOf("ny");
    const int nzIndex = vertexProperties.indexOf("nz");

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    positions.reserve(vertexCount);
    normals.reserve(vertexCount);

    for (int i = 0; i < vertexCount; ++i) {
        if (stream.atEnd())
            break;
        const QStringList tokens = stream.readLine().split(' ', Qt::SkipEmptyParts);
        if (tokens.size() < vertexProperties.size())
            continue;
        QVector3D pos;
        QVector3D norm;
        if (xIndex >= 0)
            pos.setX(tokens[xIndex].toFloat());
        if (yIndex >= 0)
            pos.setY(tokens[yIndex].toFloat());
        if (zIndex >= 0)
            pos.setZ(tokens[zIndex].toFloat());
        if (nxIndex >= 0)
            norm.setX(tokens[nxIndex].toFloat());
        if (nyIndex >= 0)
            norm.setY(tokens[nyIndex].toFloat());
        if (nzIndex >= 0)
            norm.setZ(tokens[nzIndex].toFloat());
        positions.append(pos);
        normals.append(norm);
    }

    QVector<unsigned int> indices;
    indices.reserve(faceCount * 3);
    for (int i = 0; i < faceCount; ++i) {
        if (stream.atEnd())
            break;
        const QStringList tokens = stream.readLine().split(' ', Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;
        const int vertexPerFace = tokens.first().toInt();
        if (tokens.size() < vertexPerFace + 1)
            continue;
        QVector<unsigned int> face;
        face.reserve(vertexPerFace);
        for (int j = 0; j < vertexPerFace; ++j)
            face.append(tokens[j + 1].toUInt());
        for (int j = 1; j + 1 < face.size(); ++j) {
            indices.append(face[0]);
            indices.append(face[j]);
            indices.append(face[j + 1]);
        }
    }

    if (positions.isEmpty() || indices.isEmpty()) {
        setError(QStringLiteral("PLY missing data"));
        return false;
    }

    uploadMesh(positions, normals, indices);
    return true;
}

void MeshLoader::uploadMesh(const QVector<QVector3D> &positions,
                            QVector<QVector3D> normals,
                            const QVector<unsigned int> &indices,
                            const QVector<QVector2D> &texCoords)
{
    if (positions.isEmpty() || indices.size() < 3) {
        setError(QStringLiteral("Mesh contains no triangles"));
        return;
    }

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
        computeNormals(normals, indices, positions);

    QVector<QVector2D> fullTexCoords = texCoords;
    if (fullTexCoords.size() != positions.size())
        fullTexCoords.resize(positions.size());
    qDebug() << "uploadMesh positions" << positions.size()
             << "indices" << indices.size()
             << "texcoords" << fullTexCoords.size();

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

    const QVector3D centerOffset = computeBoundsCenter(minPoint, maxPoint);
    QVector3D centeredMin(std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    QVector3D centeredMax(std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest());

    QByteArray vertexBuffer;
    vertexBuffer.resize(positions.size() * sizeof(VertexData));
    auto *vertexData = reinterpret_cast<VertexData *>(vertexBuffer.data());
    for (int i = 0; i < positions.size(); ++i) {
        const QVector3D pos = positions.at(i) - centerOffset;
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

        centeredMin.setX(qMin(centeredMin.x(), pos.x()));
        centeredMin.setY(qMin(centeredMin.y(), pos.y()));
        centeredMin.setZ(qMin(centeredMin.z(), pos.z()));
        centeredMax.setX(qMax(centeredMax.x(), pos.x()));
        centeredMax.setY(qMax(centeredMax.y(), pos.y()));
        centeredMax.setZ(qMax(centeredMax.z(), pos.z()));
    }

    QByteArray indexBuffer;
    indexBuffer.resize(indices.size() * sizeof(quint32));
    auto *indexData = reinterpret_cast<quint32 *>(indexBuffer.data());
    for (int i = 0; i < indices.size(); ++i)
        indexData[i] = indices.at(i);

    m_boundsMin = centeredMin;
    m_boundsMax = centeredMax;
    m_boundsCenter = computeBoundsCenter(centeredMin, centeredMax);
    m_boundingRadius = (centeredMax - centeredMin).length() * 0.5f;
    qDebug() << "bounds" << m_boundsMin << m_boundsMax
             << "center" << m_boundsCenter
             << "radius" << m_boundingRadius;

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
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setBounds(m_boundsMin, m_boundsMax);
    markAllDirty();

    if (!m_hasData) {
        m_hasData = true;
        emit hasDataChanged();
    }
    emit boundsChanged();
    qDebug() << "mesh bounds" << m_boundsMin << m_boundsMax << indices.size() / 3 << "triangles";

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

void MeshLoader::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}
