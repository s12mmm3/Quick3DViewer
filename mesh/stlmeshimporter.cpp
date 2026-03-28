#include "stlmeshimporter.h"

#include "logger.h"
#include "meshimporterutils.h"

#include <QDataStream>
#include <QFile>
#include <QStringConverter>
#include <QTextStream>

namespace {

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
            normal = MeshImportUtils::parseVector3(tokens, 2);
        } else if (line.startsWith(QStringLiteral("vertex"))) {
            const QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
            tri.append(MeshImportUtils::parseVector3(tokens, 1));
        } else if (line.startsWith(QStringLiteral("endfacet"))) {
            flushTriangle();
        }
    }

    if (vertices.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to parse ASCII STL"));
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
        MeshImportUtils::assignError(errorOut, QStringLiteral("STL file too small"));
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    file.seek(80);
    quint32 triangleCount = 0;
    in >> triangleCount;
    if (triangleCount == 0) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Binary STL has zero triangles"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<unsigned int> indices;

    const qint64 dataStart = file.pos();
    const quint64 payloadSize = static_cast<quint64>(file.size() - dataStart);
    const quint64 floatStride = static_cast<quint64>(sizeof(float) * 12 + sizeof(quint16));
    const quint64 doubleStride = static_cast<quint64>(sizeof(double) * 12 + sizeof(quint16));

    enum class ScalarMode {
        Float32,
        Float64
    };

    auto parseTriangles = [&](ScalarMode mode) -> bool {
        positions.clear();
        normals.clear();
        indices.clear();
        positions.reserve(triangleCount * 3);
        normals.reserve(triangleCount * 3);
        indices.reserve(triangleCount * 3);

        file.seek(dataStart);
        in.resetStatus();

        if (mode == ScalarMode::Float32)
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

    ScalarMode preferredMode = ScalarMode::Float32;
    if (triangleCount > 0 && payloadSize == expectedDoubleBytes)
        preferredMode = ScalarMode::Float64;

    bool parsed = parseTriangles(preferredMode);
    if (!parsed && preferredMode == ScalarMode::Float32) {
        WARNING << "MeshImportHelper: loadStlBinary float parse failed, retrying as double";
        parsed = parseTriangles(ScalarMode::Float64);
    }

    if (!parsed) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to read binary STL payload"));
        return false;
    }

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.indices = std::move(indices);
    result.texCoords.clear();
    result.texture = QUrl();
    return true;
}

} // namespace

bool StlMeshImporter::canLoad(const QString &suffix) const
{
    return suffix == QLatin1String("stl");
}

bool StlMeshImporter::load(const QString &filePath,
                           MeshImportResult &result,
                           QString *errorOut) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
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
