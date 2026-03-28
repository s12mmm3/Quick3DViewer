#include "meshimporthelper.h"

#include "logger.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include <utility>

namespace {

void assignError(QString *errorOut, const QString &message)
{
    if (errorOut)
        *errorOut = message;
}

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

QVector3D parseVector3(const QStringList &tokens, int offset)
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

bool parseMaterialFile(const QString &materialPath, const QDir &baseDir, QUrl &textureUrl)
{
    QFile materialFile(materialPath);
    if (!materialFile.exists())
        return false;
    if (!materialFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        WARNING << "MeshImportHelper: unable to open material file" << materialPath;
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
                textureUrl = QUrl::fromLocalFile(resolvedPath);
            else
                textureUrl = QUrl::fromUserInput(resolvedPath);
            return true;
        }
    }

    return false;
}

QUrl loadObjMaterials(const QString &geometryPath, const QStringList &materialLibs)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists() || materialLibs.isEmpty())
        return QUrl();

    for (const QString &lib : materialLibs) {
        const QString trimmed = lib.trimmed();
        if (trimmed.isEmpty())
            continue;
        QString materialPath = trimmed;
        const QFileInfo libInfo(trimmed);
        if (!libInfo.isAbsolute())
            materialPath = geometryInfo.dir().absoluteFilePath(trimmed);
        QUrl texture;
        if (parseMaterialFile(materialPath, geometryInfo.dir(), texture))
            return texture;
    }

    return QUrl();
}

QUrl loadPlyMaterial(const QString &geometryPath)
{
    QFileInfo geometryInfo(geometryPath);
    if (!geometryInfo.exists())
        return QUrl();

    const QString materialPath = geometryInfo.dir().absoluteFilePath(geometryInfo.completeBaseName() + QStringLiteral(".mtl"));
    QUrl texture;
    if (parseMaterialFile(materialPath, geometryInfo.dir(), texture))
        return texture;
    return QUrl();
}

bool loadObj(const QString &filePath,
             MeshImportResult &result,
             QString *errorOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
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
        assignError(errorOut, QStringLiteral("OBJ file has no triangles"));
        return false;
    }

    result.positions = std::move(finalPositions);
    result.normals = std::move(finalNormals);
    result.texCoords = std::move(finalTexCoords);
    result.indices = std::move(indices);
    result.texture = loadObjMaterials(filePath, materialLibraries);
    return true;
}

bool loadStlAscii(QFile &file,
                  MeshImportResult &result,
                  QString *errorOut)
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
        assignError(errorOut, QStringLiteral("Failed to parse ASCII STL"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<unsigned int> finalIndices = indices;
    positions.reserve(vertices.size());
    normals.reserve(vertices.size());

    for (const auto &v : std::as_const(vertices)) {
        positions.append(v.position);
        normals.append(v.normal);
    }

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.indices = std::move(finalIndices);
    result.texCoords.clear();
    result.texture = QUrl();
    return true;
}

bool loadStlBinary(QFile &file,
                   MeshImportResult &result,
                   QString *errorOut)
{
    if (file.size() < 84) {
        assignError(errorOut, QStringLiteral("STL file too small"));
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    file.seek(80);
    quint32 triangleCount = 0;
    in >> triangleCount;
    if (triangleCount == 0) {
        assignError(errorOut, QStringLiteral("Binary STL has zero triangles"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<unsigned int> indices;

    const qint64 dataStart = file.pos();
    const quint64 payloadSize = static_cast<quint64>(file.size() - dataStart);
    const quint64 floatStride = static_cast<quint64>(sizeof(float) * 12 + sizeof(quint16));  // 12 floats + attr
    const quint64 doubleStride = static_cast<quint64>(sizeof(double) * 12 + sizeof(quint16));

    enum class StlScalarMode {
        Float32,
        Float64
    };

    auto parseTriangles = [&](StlScalarMode mode) -> bool {
        positions.clear();
        normals.clear();
        indices.clear();
        positions.reserve(triangleCount * 3);
        normals.reserve(triangleCount * 3);
        indices.reserve(triangleCount * 3);

        file.seek(dataStart);
        in.resetStatus();

        if (mode == StlScalarMode::Float32)
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
        else
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);

        for (quint32 i = 0; i < triangleCount; ++i) {
            double nx = 0.0;
            double ny = 0.0;
            double nz = 0.0;
            in >> nx >> ny >> nz;

            QVector3D normal(static_cast<float>(nx),
                             static_cast<float>(ny),
                             static_cast<float>(nz));

            QVector3D verts[3];
            for (int v = 0; v < 3; ++v) {
                double vx = 0.0;
                double vy = 0.0;
                double vz = 0.0;
                in >> vx >> vy >> vz;
                verts[v] = QVector3D(static_cast<float>(vx),
                                     static_cast<float>(vy),
                                     static_cast<float>(vz));
            }

            quint16 attr = 0;
            in >> attr;

            if (in.status() != QDataStream::Ok)
                return false;

            const QVector3D computedNormal = QVector3D::normal(verts[0], verts[1], verts[2]);
            const QVector3D faceNormal = normal.isNull() ? computedNormal : normal.normalized();
            for (int v = 0; v < 3; ++v) {
                positions.append(verts[v]);
                normals.append(faceNormal);
                indices.append(positions.size() - 1);
            }
        }

        return true;
    };

    const quint64 expectedFloatBytes = static_cast<quint64>(triangleCount) * floatStride;
    const quint64 expectedDoubleBytes = static_cast<quint64>(triangleCount) * doubleStride;

    StlScalarMode preferredMode = StlScalarMode::Float32;
    if (triangleCount > 0 && payloadSize == expectedDoubleBytes)
        preferredMode = StlScalarMode::Float64;

    bool parsed = parseTriangles(preferredMode);
    if (!parsed && preferredMode == StlScalarMode::Float32) {
        WARNING << "MeshImportHelper: loadStlBinary float parse failed, retrying as double";
        parsed = parseTriangles(StlScalarMode::Float64);
    }

    if (!parsed) {
        assignError(errorOut, QStringLiteral("Failed to read binary STL payload"));
        return false;
    }

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.indices = std::move(indices);
    result.texCoords.clear();
    result.texture = QUrl();
    return true;
}

bool loadStl(const QString &filePath,
             MeshImportResult &result,
             QString *errorOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    const QByteArray header = file.peek(80);
    const bool asciiCandidate = header.trimmed().startsWith("solid");

    if (asciiCandidate) {
        file.seek(0);
        if (loadStlAscii(file, result, errorOut))
            return true;
        file.seek(0);
    }

    file.seek(0);
    return loadStlBinary(file, result, errorOut);
}

bool applyTextureHint(const QString &hint,
                      const QString &geometryPath,
                      QUrl &textureOut)
{
    if (hint.isEmpty())
        return false;

    QString texturePath = hint;
    if (texturePath.startsWith(QLatin1Char('"')) && texturePath.endsWith(QLatin1Char('"')) && texturePath.size() >= 2)
        texturePath = texturePath.mid(1, texturePath.size() - 2);
    if (texturePath.isEmpty())
        return false;

    QFileInfo geometryInfo(geometryPath);
    QString resolved = texturePath;
    QFileInfo textureInfo(texturePath);
    if (!textureInfo.isAbsolute())
        resolved = geometryInfo.dir().absoluteFilePath(texturePath);
    if (QFile::exists(resolved))
        textureOut = QUrl::fromLocalFile(resolved);
    else
        textureOut = QUrl::fromUserInput(texturePath);
    return true;
}

bool loadPly(const QString &filePath,
             MeshImportResult &result,
             QString *errorOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
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
        assignError(errorOut, QStringLiteral("Not a PLY file"));
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
        assignError(errorOut, QStringLiteral("Unsupported PLY format"));
        return false;
    }

    if (dataStartPos < 0) {
        assignError(errorOut, QStringLiteral("PLY header missing data section"));
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
            assignError(errorOut, QStringLiteral("Failed to seek PLY data section"));
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
            assignError(errorOut, QStringLiteral("Failed to seek PLY data section"));
            return false;
        }
        QDataStream bin(&file);
        bin.setFloatingPointPrecision(QDataStream::SinglePrecision);
        if (format == PlyFormat::BinaryLittle)
            bin.setByteOrder(QDataStream::LittleEndian);
        else
            bin.setByteOrder(QDataStream::BigEndian);

        if (vertexPropertyDefs.isEmpty()) {
            assignError(errorOut, QStringLiteral("PLY vertex properties missing"));
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
            assignError(errorOut, QStringLiteral("PLY face indices property missing"));
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
            assignError(errorOut, QStringLiteral("Failed to read binary PLY data"));
            return false;
        }
    }

    if (positions.isEmpty() || indices.isEmpty()) {
        assignError(errorOut, QStringLiteral("PLY missing data"));
        return false;
    }

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.indices = std::move(indices);
    result.texCoords.clear();

    QUrl texture;
    if (!applyTextureHint(textureFileHint, filePath, texture))
        texture = loadPlyMaterial(filePath);
    result.texture = texture;
    return true;
}

} // namespace

bool MeshImportHelper::load(const QString &filePath,
                            MeshImportResult &result,
                            QString *errorOut)
{
    result = MeshImportResult();
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    bool ok = false;
    if (suffix == QLatin1String("obj")) {
        ok = loadObj(filePath, result, errorOut);
    } else if (suffix == QLatin1String("stl")) {
        ok = loadStl(filePath, result, errorOut);
    } else if (suffix == QLatin1String("ply")) {
        ok = loadPly(filePath, result, errorOut);
    } else {
        assignError(errorOut, QStringLiteral("Unsupported extension: %1").arg(suffix));
        return false;
    }

    if (!ok && errorOut && errorOut->isEmpty())
        *errorOut = QStringLiteral("Failed to load mesh: %1").arg(filePath);
    return ok;
}
