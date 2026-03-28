#include "objmeshimporter.h"

#include "meshimporterutils.h"

#include <QFile>
#include <QHash>
#include <QStringConverter>
#include <QTextStream>

namespace {
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

bool ObjMeshImporter::canLoad(const QString &suffix) const
{
    return suffix == QLatin1String("obj");
}

bool ObjMeshImporter::load(const QString &filePath,
                           MeshImportResult &result,
                           QString *errorOut) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
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
            positions.append(MeshImportUtils::parseVector3(tokens, 1));
        } else if (type == QLatin1String("vn")) {
            if (tokens.size() < 4)
                continue;
            normalsSource.append(MeshImportUtils::parseVector3(tokens, 1));
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
        MeshImportUtils::assignError(errorOut, QStringLiteral("OBJ file has no triangles"));
        return false;
    }

    result.positions = std::move(finalPositions);
    result.normals = std::move(finalNormals);
    result.texCoords = std::move(finalTexCoords);
    result.indices = std::move(indices);
    result.texture = MeshImportUtils::loadObjMaterials(filePath, materialLibraries);
    return true;
}
