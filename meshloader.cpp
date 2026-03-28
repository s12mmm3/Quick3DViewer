#include "meshloader.h"
#include "logger.h"

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QStringConverter>
#include <QDir>
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

struct PlyPropertyDef
{
    QString name;
    QString type;
    bool isList = false;
    QString listCountType;
    QString listValueType;
};

static double readPlyScalarValue(QDataStream &stream, const QString &type)
{
    const QString t = type.toLower();
    if (t == QLatin1String("char") || t == QLatin1String("int8")) {
        qint8 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("uchar") || t == QLatin1String("uint8")) {
        quint8 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("short") || t == QLatin1String("int16")) {
        qint16 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("ushort") || t == QLatin1String("uint16")) {
        quint16 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("int") || t == QLatin1String("int32")) {
        qint32 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("uint") || t == QLatin1String("uint32")) {
        quint32 value = 0;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("float") || t == QLatin1String("float32")) {
        float value = 0.0f;
        stream >> value;
        return value;
    }
    if (t == QLatin1String("double") || t == QLatin1String("float64")) {
        double value = 0.0;
        stream >> value;
        return value;
    }

    float fallback = 0.0f;
    stream >> fallback;
    return fallback;
}

static quint32 readPlyCountValue(QDataStream &stream, const QString &type)
{
    const double value = readPlyScalarValue(stream, type);
    return value < 0.0 ? 0u : static_cast<quint32>(value);
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
        WARNING << "MeshLoader: failed to load" << filePath << m_error;
}

QString MeshLoader::errorString() const
{
    return m_error;
}

QUrl MeshLoader::colorTexture() const
{
    return m_colorTexture;
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
    setColorTexture(QUrl());
    if (previousHasData)
        emit hasDataChanged();
    emit boundsChanged();
    setError(QString());
}

bool MeshLoader::loadFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    bool ok = false;

    DEBUG << "MeshLoader loading" << filePath << "as" << suffix;
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
        DEBUG << "MeshLoader load success" << filePath;
    } else {
        WARNING << "MeshLoader load failed" << filePath << m_error;
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
    QStringList materialLibraries;

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
        } else if (type == QLatin1String("mtllib")) {
            const QString remainder = line.mid(type.length()).trimmed();
            if (!remainder.isEmpty()) {
                const QStringList libs = remainder.split(' ', Qt::SkipEmptyParts);
                for (const QString &lib : libs)
                    materialLibraries.append(lib);
            }
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
    loadObjMaterials(filePath, materialLibraries);
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
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    auto readTrimmedLine = [&file]() -> QString {
        if (file.atEnd())
            return QString();
        const QByteArray raw = file.readLine();
        return QString::fromUtf8(raw).trimmed();
    };

    QString line = readTrimmedLine();
    if (line != QLatin1String("ply")) {
        setError(QStringLiteral("Not a PLY file"));
        return false;
    }

    enum class PlyFormat {
        Unknown,
        Ascii,
        BinaryLittle,
        BinaryBig
    };

    PlyFormat format = PlyFormat::Unknown;
    int vertexCount = 0;
    int faceCount = 0;
    QStringList vertexProperties;
    QVector<PlyPropertyDef> vertexPropertyDefs;
    QVector<PlyPropertyDef> facePropertyDefs;
    int faceIndicesPropertyIndex = -1;
    bool parsingVertexProperties = false;
    bool parsingFaceProperties = false;
    QString textureFileHint;
    qint64 dataStartPos = -1;

    while (!file.atEnd()) {
        line = readTrimmedLine();
        if (line == QLatin1String("end_header")) {
            dataStartPos = file.pos();
            break;
        }
        if (line.startsWith(QStringLiteral("comment"), Qt::CaseInsensitive)) {
            const QString commentBody = line.mid(QStringLiteral("comment").length()).trimmed();
            if (commentBody.startsWith(QStringLiteral("TextureFile"), Qt::CaseInsensitive)) {
                textureFileHint = commentBody.mid(QStringLiteral("TextureFile").length()).trimmed();
                continue;
            }
        }
        if (line.startsWith(QStringLiteral("format"))) {
            if (line.contains(QStringLiteral("ascii"), Qt::CaseInsensitive))
                format = PlyFormat::Ascii;
            else if (line.contains(QStringLiteral("binary_little_endian"), Qt::CaseInsensitive))
                format = PlyFormat::BinaryLittle;
            else if (line.contains(QStringLiteral("binary_big_endian"), Qt::CaseInsensitive))
                format = PlyFormat::BinaryBig;
        } else if (line.startsWith(QStringLiteral("element"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            if (tokens.size() >= 3) {
                if (tokens[1] == QLatin1String("vertex")) {
                    vertexCount = tokens[2].toInt();
                    vertexProperties.clear();
                    vertexPropertyDefs.clear();
                    parsingVertexProperties = true;
                    parsingFaceProperties = false;
                } else if (tokens[1] == QLatin1String("face")) {
                    faceCount = tokens[2].toInt();
                    facePropertyDefs.clear();
                    faceIndicesPropertyIndex = -1;
                    parsingVertexProperties = false;
                    parsingFaceProperties = true;
                } else {
                    parsingVertexProperties = false;
                    parsingFaceProperties = false;
                }
            }
        } else if (line.startsWith(QStringLiteral("property"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            if (parsingVertexProperties) {
                if (tokens.size() >= 3) {
                    PlyPropertyDef prop;
                    prop.name = tokens.last();
                    prop.type = tokens.at(tokens.size() - 2);
                    vertexProperties.append(prop.name);
                    vertexPropertyDefs.append(prop);
                }
            } else if (parsingFaceProperties) {
                if (tokens.size() >= 5 && tokens[1] == QLatin1String("list")) {
                    PlyPropertyDef prop;
                    prop.isList = true;
                    prop.listCountType = tokens[2];
                    prop.listValueType = tokens[3];
                    prop.name = tokens[4];
                    facePropertyDefs.append(prop);
                    if (faceIndicesPropertyIndex == -1 &&
                        (prop.name.contains(QStringLiteral("vertex"), Qt::CaseInsensitive) ||
                         prop.name.contains(QStringLiteral("indices"), Qt::CaseInsensitive))) {
                        faceIndicesPropertyIndex = facePropertyDefs.size() - 1;
                    }
                } else if (tokens.size() >= 3) {
                    PlyPropertyDef prop;
                    prop.name = tokens.last();
                    prop.type = tokens.at(tokens.size() - 2);
                    facePropertyDefs.append(prop);
                }
            }
        }
    }

    if (format == PlyFormat::Unknown) {
        setError(QStringLiteral("Unsupported PLY format"));
        return false;
    }

    if (dataStartPos < 0) {
        setError(QStringLiteral("PLY header missing data section"));
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

    QVector<unsigned int> indices;
    indices.reserve(faceCount * 3);

    if (format == PlyFormat::Ascii) {
        if (!file.seek(dataStartPos)) {
            setError(QStringLiteral("Failed to seek PLY data section"));
            return false;
        }
        QTextStream dataStream(&file);
        dataStream.setEncoding(QStringConverter::Utf8);
        for (int i = 0; i < vertexCount; ++i) {
            if (dataStream.atEnd())
                break;
            const QStringList tokens = dataStream.readLine().split(' ', Qt::SkipEmptyParts);
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

        for (int i = 0; i < faceCount; ++i) {
            if (dataStream.atEnd())
                break;
            const QStringList tokens = dataStream.readLine().split(' ', Qt::SkipEmptyParts);
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
    } else {
        if (!file.seek(dataStartPos)) {
            setError(QStringLiteral("Failed to seek PLY data section"));
            return false;
        }
        QDataStream bin(&file);
        bin.setFloatingPointPrecision(QDataStream::SinglePrecision);
        if (format == PlyFormat::BinaryLittle)
            bin.setByteOrder(QDataStream::LittleEndian);
        else
            bin.setByteOrder(QDataStream::BigEndian);

        if (vertexPropertyDefs.isEmpty()) {
            setError(QStringLiteral("PLY vertex properties missing"));
            return false;
        }

        for (int i = 0; i < vertexCount; ++i) {
            QVector3D pos;
            QVector3D norm;
            for (const PlyPropertyDef &prop : std::as_const(vertexPropertyDefs)) {
                if (prop.isList) {
                    const quint32 count = readPlyCountValue(bin, prop.listCountType);
                    for (quint32 c = 0; c < count; ++c)
                        readPlyScalarValue(bin, prop.listValueType);
                    continue;
                }
                const double value = readPlyScalarValue(bin, prop.type);
                const QString name = prop.name.toLower();
                if (name == QLatin1String("x"))
                    pos.setX(value);
                else if (name == QLatin1String("y"))
                    pos.setY(value);
                else if (name == QLatin1String("z"))
                    pos.setZ(value);
                else if (name == QLatin1String("nx"))
                    norm.setX(value);
                else if (name == QLatin1String("ny"))
                    norm.setY(value);
                else if (name == QLatin1String("nz"))
                    norm.setZ(value);
            }
            positions.append(pos);
            normals.append(norm);
        }

        if (facePropertyDefs.isEmpty() || faceIndicesPropertyIndex < 0) {
            setError(QStringLiteral("PLY face indices property missing"));
            return false;
        }

        for (int i = 0; i < faceCount; ++i) {
            QVector<unsigned int> face;
            for (int p = 0; p < facePropertyDefs.size(); ++p) {
                const PlyPropertyDef &prop = facePropertyDefs.at(p);
                if (prop.isList) {
                    const quint32 count = readPlyCountValue(bin, prop.listCountType);
                    if (p == faceIndicesPropertyIndex) {
                        face.reserve(count);
                        for (quint32 c = 0; c < count; ++c)
                            face.append(readPlyCountValue(bin, prop.listValueType));
                    } else {
                        for (quint32 c = 0; c < count; ++c)
                            readPlyScalarValue(bin, prop.listValueType);
                    }
                } else {
                    readPlyScalarValue(bin, prop.type);
                }
            }
            for (int j = 1; j + 1 < face.size(); ++j) {
                indices.append(face[0]);
                indices.append(face[j]);
                indices.append(face[j + 1]);
            }
        }

        if (bin.status() != QDataStream::Ok) {
            setError(QStringLiteral("Failed to read binary PLY data"));
            return false;
        }
    }

    if (positions.isEmpty() || indices.isEmpty()) {
        setError(QStringLiteral("PLY missing data"));
        return false;
    }

    uploadMesh(positions, normals, indices);

    auto applyTextureHint = [&](const QString &hint) -> bool {
        if (hint.isEmpty())
            return false;
        QString texturePath = hint;
        if (texturePath.startsWith(QLatin1Char('"')) && texturePath.endsWith(QLatin1Char('"')) && texturePath.size() >= 2)
            texturePath = texturePath.mid(1, texturePath.size() - 2);
        if (texturePath.isEmpty())
            return false;
        QFileInfo geometryInfo(filePath);
        QString resolved = texturePath;
        QFileInfo textureInfo(texturePath);
        if (!textureInfo.isAbsolute())
            resolved = geometryInfo.dir().absoluteFilePath(texturePath);
        if (QFile::exists(resolved))
            setColorTexture(QUrl::fromLocalFile(resolved));
        else
            setColorTexture(QUrl::fromUserInput(texturePath));
        return true;
    };

    if (!applyTextureHint(textureFileHint))
        loadPlyMaterial(filePath);
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

    const QVector<unsigned int> cleanedIndices = sanitizeIndices(indices, positions);
    if (cleanedIndices.size() < 3) {
        setError(QStringLiteral("Mesh contains no valid triangles"));
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

    m_boundsMin = minPoint;
    m_boundsMax = maxPoint;
    m_boundsCenter = computeBoundsCenter(minPoint, maxPoint);
    m_boundingRadius = (maxPoint - minPoint).length() * 0.5f;
    DEBUG << "bounds" << m_boundsMin << m_boundsMax
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
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0,
                 QQuick3DGeometry::Attribute::U32Type);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setBounds(m_boundsMin, m_boundsMax);
    markAllDirty();

    if (!m_hasData) {
        m_hasData = true;
        emit hasDataChanged();
    }
    emit boundsChanged();
    DEBUG << "mesh bounds" << m_boundsMin << m_boundsMax << finalIndices.size() / 3 << "triangles";

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

void MeshLoader::setColorTexture(const QUrl &textureUrl)
{
    if (m_colorTexture == textureUrl)
        return;
    m_colorTexture = textureUrl;
    emit colorTextureChanged();
}

void MeshLoader::loadPlyMaterial(const QString &geometryPath)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists()) {
        setColorTexture(QUrl());
        return;
    }

    const QString materialPath = geometryInfo.dir().absoluteFilePath(geometryInfo.completeBaseName() + QStringLiteral(".mtl"));
    if (!parseMaterialFile(materialPath, geometryInfo.dir()))
        setColorTexture(QUrl());
}

void MeshLoader::loadObjMaterials(const QString &geometryPath, const QStringList &materialLibs)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists()) {
        setColorTexture(QUrl());
        return;
    }

    if (materialLibs.isEmpty()) {
        setColorTexture(QUrl());
        return;
    }

    bool loadedTexture = false;
    for (const QString &lib : materialLibs) {
        const QString trimmed = lib.trimmed();
        if (trimmed.isEmpty())
            continue;
        QString materialPath = trimmed;
        const QFileInfo libInfo(trimmed);
        if (!libInfo.isAbsolute())
            materialPath = geometryInfo.dir().absoluteFilePath(trimmed);
        if (parseMaterialFile(materialPath, geometryInfo.dir())) {
            loadedTexture = true;
            break;
        }
    }

    if (!loadedTexture)
        setColorTexture(QUrl());
}

bool MeshLoader::parseMaterialFile(const QString &materialPath, const QDir &baseDir)
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

            QString resolvedPath = texturePath;
            const QFileInfo texInfo(texturePath);
            if (!texInfo.isAbsolute())
                resolvedPath = baseDir.absoluteFilePath(texturePath);

            if (QFile::exists(resolvedPath))
                setColorTexture(QUrl::fromLocalFile(resolvedPath));
            else
                setColorTexture(QUrl::fromUserInput(resolvedPath));
            return true;
        }
    }

    return false;
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
