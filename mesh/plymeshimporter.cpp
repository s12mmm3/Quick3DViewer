#include "plymeshimporter.h"

#include "meshimporterutils.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>

namespace {

struct PlyPropertyDef
{
    QString name;
    QString type;
    bool isList = false;
    QString listCountType;
    QString listValueType;
};

double readPlyScalarValue(QDataStream &stream, const QString &type)
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

quint32 readPlyCountValue(QDataStream &stream, const QString &type)
{
    const double value = readPlyScalarValue(stream, type);
    return value < 0.0 ? 0u : static_cast<quint32>(value);
}

enum class PlyFormat {
    Unknown,
    Ascii,
    BinaryLittle,
    BinaryBig
};

} // namespace

bool PlyMeshImporter::canLoad(const QString &suffix) const
{
    return suffix == QLatin1String("ply");
}

bool PlyMeshImporter::load(const QString &filePath,
                           MeshImportResult &result,
                           QString *errorOut) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
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
        MeshImportUtils::assignError(errorOut, QStringLiteral("Not a PLY file"));
        return false;
    }

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
        MeshImportUtils::assignError(errorOut, QStringLiteral("Unsupported PLY format"));
        return false;
    }

    if (dataStartPos < 0) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("PLY header missing data section"));
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
            MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to seek PLY data section"));
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
            MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to seek PLY data section"));
            return false;
        }
        QDataStream bin(&file);
        bin.setFloatingPointPrecision(QDataStream::SinglePrecision);
        if (format == PlyFormat::BinaryLittle)
            bin.setByteOrder(QDataStream::LittleEndian);
        else
            bin.setByteOrder(QDataStream::BigEndian);

        if (vertexPropertyDefs.isEmpty()) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("PLY vertex properties missing"));
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
            MeshImportUtils::assignError(errorOut, QStringLiteral("PLY face indices property missing"));
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
            MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to read binary PLY data"));
            return false;
        }
    }

    if (positions.isEmpty() || indices.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("PLY missing data"));
        return false;
    }

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.indices = std::move(indices);
    result.texCoords.clear();

    QUrl texture;
    if (!textureFileHint.isEmpty()) {
        if (textureFileHint.startsWith(QLatin1Char('"')) && textureFileHint.endsWith(QLatin1Char('"')) && textureFileHint.size() >= 2)
            textureFileHint = textureFileHint.mid(1, textureFileHint.size() - 2);
        QFileInfo geometryInfo(filePath);
        textureFileHint = MeshImportUtils::normalizePath(textureFileHint);
        QString resolved = textureFileHint;
        QFileInfo textureInfo(resolved);
        if (!textureInfo.isAbsolute()) {
            resolved = MeshImportUtils::normalizePath(geometryInfo.dir().absoluteFilePath(textureFileHint));
            textureInfo.setFile(resolved);
        }
        if (textureInfo.exists())
            texture = QUrl::fromLocalFile(resolved);
        else
            texture = QUrl::fromUserInput(textureFileHint);
    }
    if (texture.isEmpty())
        texture = MeshImportUtils::loadPlyMaterial(filePath);
    result.texture = texture;
    return true;
}
