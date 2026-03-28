#include "gltfmeshimporter.h"

#include "meshimporterutils.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include <cstring>

namespace {

struct GltfBufferView
{
    int buffer = -1;
    int byteOffset = 0;
    int byteLength = 0;
    int byteStride = 0;
};

struct GltfAccessor
{
    int bufferView = -1;
    int byteOffset = 0;
    int componentType = 0;
    int count = 0;
    QString type;
    bool normalized = false;
};

int gltfComponentSize(int componentType)
{
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

float normalizeInteger(int componentType, bool normalized, qint64 value)
{
    if (!normalized)
        return static_cast<float>(value);

    switch (componentType) {
    case 5120:
        return qBound(-1.0f, static_cast<float>(value) / 127.0f, 1.0f);
    case 5121:
        return static_cast<float>(value) / 255.0f;
    case 5122:
        if (value <= -32768)
            return -1.0f;
        return qBound(-1.0f, static_cast<float>(value) / 32767.0f, 1.0f);
    case 5123:
        return static_cast<float>(value) / 65535.0f;
    case 5125:
        return static_cast<float>(value) / 4294967295.0f;
    default:
        break;
    }
    return static_cast<float>(value);
}

float readComponentAsFloat(const uchar *ptr, int componentType, bool normalized)
{
    switch (componentType) {
    case 5126: {
        float v = 0.0f;
        std::memcpy(&v, ptr, sizeof(float));
        return v;
    }
    case 5120: {
        qint8 v = 0;
        std::memcpy(&v, ptr, sizeof(qint8));
        return normalizeInteger(componentType, normalized, v);
    }
    case 5121: {
        quint8 v = 0;
        std::memcpy(&v, ptr, sizeof(quint8));
        return normalizeInteger(componentType, normalized, v);
    }
    case 5122: {
        qint16 v = 0;
        std::memcpy(&v, ptr, sizeof(qint16));
        return normalizeInteger(componentType, normalized, v);
    }
    case 5123: {
        quint16 v = 0;
        std::memcpy(&v, ptr, sizeof(quint16));
        return normalizeInteger(componentType, normalized, v);
    }
    case 5125: {
        quint32 v = 0;
        std::memcpy(&v, ptr, sizeof(quint32));
        return normalizeInteger(componentType, normalized, v);
    }
    default:
        break;
    }
    return 0.0f;
}

unsigned int readIndexValue(const uchar *ptr, int componentType)
{
    switch (componentType) {
    case 5121: {
        quint8 v = 0;
        std::memcpy(&v, ptr, sizeof(quint8));
        return v;
    }
    case 5123: {
        quint16 v = 0;
        std::memcpy(&v, ptr, sizeof(quint16));
        return v;
    }
    case 5125: {
        quint32 v = 0;
        std::memcpy(&v, ptr, sizeof(quint32));
        return v;
    }
    default:
        break;
    }
    return 0;
}

bool decodeDataUri(const QString &uri,
                   QByteArray &out,
                   QString *errorOut,
                   QString *mimeOut = nullptr)
{
    const int commaIndex = uri.indexOf(',');
    if (commaIndex < 0) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Invalid data URI in glTF buffer"));
        return false;
    }
    QString mime = QStringLiteral("application/octet-stream");
    const QStringView prefix = QStringView(uri).mid(5, commaIndex - 5);
    const int semicolonIndex = prefix.indexOf(QLatin1Char(';'));
    if (semicolonIndex >= 0) {
        const QStringView maybeMime = prefix.left(semicolonIndex);
        if (!maybeMime.isEmpty())
            mime = maybeMime.toString();
    } else if (!prefix.isEmpty()) {
        mime = prefix.toString();
    }
    if (mimeOut)
        *mimeOut = mime;
    out = QByteArray::fromBase64(QStringView(uri).mid(commaIndex + 1).toUtf8());
    if (out.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to decode base64 buffer in glTF"));
        return false;
    }
    return true;
}

bool loadBufferUri(const QString &basePath,
                   const QString &uri,
                   QByteArray &out,
                   QString *errorOut)
{
    if (uri.startsWith("data:", Qt::CaseInsensitive))
        return decodeDataUri(uri, out, errorOut);

    const QFileInfo bufInfo(uri);
    QString resolved = uri;
    if (!bufInfo.isAbsolute())
        resolved = QDir(basePath).absoluteFilePath(uri);
    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly)) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open glTF buffer %1").arg(resolved));
        return false;
    }
    out = file.readAll();
    return true;
}

bool parseGlbFile(const QString &filePath,
                  QJsonDocument &doc,
                  QVector<QByteArray> &binaryChunks,
                  QString *errorOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 length = 0;
    stream >> magic >> version >> length;
    if (magic != 0x46546C67 || version != 2) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Invalid GLB header"));
        return false;
    }

    bool jsonFound = false;
    while (!stream.atEnd()) {
        quint32 chunkLength = 0;
        quint32 chunkType = 0;
        stream >> chunkLength >> chunkType;
        if (stream.status() != QDataStream::Ok || chunkLength == 0) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Corrupt GLB chunk header"));
            return false;
        }
        QByteArray chunkData = file.read(chunkLength);
        if (chunkData.size() != static_cast<int>(chunkLength)) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to read GLB chunk data"));
            return false;
        }
        if (chunkType == 0x4E4F534A) {
            QJsonParseError err;
            doc = QJsonDocument::fromJson(chunkData, &err);
            if (err.error != QJsonParseError::NoError) {
                MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to parse GLB JSON chunk: %1").arg(err.errorString()));
                return false;
            }
            jsonFound = true;
        } else if (chunkType == 0x004E4942) {
            binaryChunks.append(chunkData);
        }
        const qint64 padding = (4 - (chunkLength % 4)) % 4;
        if (padding > 0)
            file.seek(file.pos() + padding);
    }

    if (!jsonFound) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("GLB file missing JSON chunk"));
        return false;
    }
    return true;
}

bool loadGltfBuffers(const QJsonDocument &doc,
                     const QVector<QByteArray> &glbBinaryChunks,
                     const QString &basePath,
                     QVector<QByteArray> &buffersOut,
                     QString *errorOut)
{
    const QJsonArray buffers = doc.object().value(QStringLiteral("buffers")).toArray();
    if (buffers.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF file has no buffers"));
        return false;
    }
    buffersOut.clear();
    buffersOut.reserve(buffers.size());
    int glbChunkIndex = 0;
    for (int i = 0; i < buffers.size(); ++i) {
        const QJsonObject bufObj = buffers.at(i).toObject();
        const QString uri = bufObj.value(QStringLiteral("uri")).toString();
        QByteArray data;
        if (!uri.isEmpty()) {
            if (!loadBufferUri(basePath, uri, data, errorOut))
                return false;
        } else {
            if (glbChunkIndex >= glbBinaryChunks.size()) {
                MeshImportUtils::assignError(errorOut, QStringLiteral("glTF buffer %1 missing URI and binary chunk").arg(i));
                return false;
            }
            data = glbBinaryChunks[glbChunkIndex++];
        }
        if (data.isEmpty()) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("glTF buffer %1 is empty").arg(i));
            return false;
        }
        buffersOut.append(data);
    }
    return true;
}

bool extractGltfAccessors(const QJsonDocument &doc,
                          QVector<GltfBufferView> &bufferViews,
                          QVector<GltfAccessor> &accessors,
                          QString *errorOut)
{
    const QJsonArray bufferViewArray = doc.object().value(QStringLiteral("bufferViews")).toArray();
    bufferViews.resize(bufferViewArray.size());
    for (int i = 0; i < bufferViewArray.size(); ++i) {
        const QJsonObject viewObj = bufferViewArray.at(i).toObject();
        GltfBufferView view;
        view.buffer = viewObj.value(QStringLiteral("buffer")).toInt(-1);
        view.byteOffset = viewObj.value(QStringLiteral("byteOffset")).toInt(0);
        view.byteLength = viewObj.value(QStringLiteral("byteLength")).toInt(0);
        view.byteStride = viewObj.value(QStringLiteral("byteStride")).toInt(0);
        if (view.buffer < 0 || view.byteLength <= 0) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Invalid glTF bufferView at index %1").arg(i));
            return false;
        }
        bufferViews[i] = view;
    }

    const QJsonArray accessorArray = doc.object().value(QStringLiteral("accessors")).toArray();
    accessors.resize(accessorArray.size());
    for (int i = 0; i < accessorArray.size(); ++i) {
        const QJsonObject accObj = accessorArray.at(i).toObject();
        GltfAccessor accessor;
        accessor.bufferView = accObj.value(QStringLiteral("bufferView")).toInt(-1);
        accessor.byteOffset = accObj.value(QStringLiteral("byteOffset")).toInt(0);
        accessor.componentType = accObj.value(QStringLiteral("componentType")).toInt();
        accessor.count = accObj.value(QStringLiteral("count")).toInt();
        accessor.type = accObj.value(QStringLiteral("type")).toString();
        accessor.normalized = accObj.value(QStringLiteral("normalized")).toBool(false);
        if (accessor.bufferView < 0 || accessor.count <= 0 || accessor.componentType == 0 || accessor.type.isEmpty()) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Invalid glTF accessor at index %1").arg(i));
            return false;
        }
        accessors[i] = accessor;
    }
    return true;
}

bool readAccessorVec3(const QVector<QByteArray> &buffers,
                      const QVector<GltfBufferView> &bufferViews,
                      const QVector<GltfAccessor> &accessors,
                      int accessorIndex,
                      QVector<QVector3D> &out,
                      QString *errorOut)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF accessor index out of range"));
        return false;
    }
    const GltfAccessor &accessor = accessors.at(accessorIndex);
    if (accessor.type != QLatin1String("VEC3")) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Expected VEC3 accessor"));
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= bufferViews.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor missing bufferView"));
        return false;
    }
    if (accessor.componentType != 5126 && accessor.componentType != 5120 &&
        accessor.componentType != 5121 && accessor.componentType != 5122 &&
        accessor.componentType != 5123 && accessor.componentType != 5125) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Unsupported component type for VEC3 accessor"));
        return false;
    }
    const GltfBufferView &view = bufferViews.at(accessor.bufferView);
    if (view.buffer < 0 || view.buffer >= buffers.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("BufferView references invalid buffer"));
        return false;
    }
    const QByteArray &buffer = buffers.at(view.buffer);
    const int componentSize = gltfComponentSize(accessor.componentType);
    const int elementSize = componentSize * 3;
    const int stride = view.byteStride > 0 ? view.byteStride : elementSize;
    const int required = accessor.byteOffset + view.byteOffset + stride * accessor.count;
    if (required > buffer.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor exceeds buffer bounds"));
        return false;
    }
    out.reserve(out.size() + accessor.count);
    const uchar *data = reinterpret_cast<const uchar *>(buffer.constData()) + view.byteOffset + accessor.byteOffset;
    for (int i = 0; i < accessor.count; ++i) {
        const uchar *ptr = data + stride * i;
        QVector3D value;
        value.setX(readComponentAsFloat(ptr + componentSize * 0, accessor.componentType, accessor.normalized));
        value.setY(readComponentAsFloat(ptr + componentSize * 1, accessor.componentType, accessor.normalized));
        value.setZ(readComponentAsFloat(ptr + componentSize * 2, accessor.componentType, accessor.normalized));
        out.append(value);
    }
    return true;
}

bool readAccessorVec2(const QVector<QByteArray> &buffers,
                      const QVector<GltfBufferView> &bufferViews,
                      const QVector<GltfAccessor> &accessors,
                      int accessorIndex,
                      QVector<QVector2D> &out,
                      QString *errorOut)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF accessor index out of range"));
        return false;
    }
    const GltfAccessor &accessor = accessors.at(accessorIndex);
    if (accessor.type != QLatin1String("VEC2")) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Expected VEC2 accessor"));
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= bufferViews.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor missing bufferView"));
        return false;
    }
    if (accessor.componentType != 5126 && accessor.componentType != 5120 &&
        accessor.componentType != 5121 && accessor.componentType != 5122 &&
        accessor.componentType != 5123 && accessor.componentType != 5125) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Unsupported component type for VEC2 accessor"));
        return false;
    }
    const GltfBufferView &view = bufferViews.at(accessor.bufferView);
    if (view.buffer < 0 || view.buffer >= buffers.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("BufferView references invalid buffer"));
        return false;
    }
    const QByteArray &buffer = buffers.at(view.buffer);
    const int componentSize = gltfComponentSize(accessor.componentType);
    const int elementSize = componentSize * 2;
    const int stride = view.byteStride > 0 ? view.byteStride : elementSize;
    const int required = accessor.byteOffset + view.byteOffset + stride * accessor.count;
    if (required > buffer.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor exceeds buffer bounds"));
        return false;
    }
    out.reserve(out.size() + accessor.count);
    const uchar *data = reinterpret_cast<const uchar *>(buffer.constData()) + view.byteOffset + accessor.byteOffset;
    for (int i = 0; i < accessor.count; ++i) {
        const uchar *ptr = data + stride * i;
        QVector2D value;
        value.setX(readComponentAsFloat(ptr + componentSize * 0, accessor.componentType, accessor.normalized));
        value.setY(readComponentAsFloat(ptr + componentSize * 1, accessor.componentType, accessor.normalized));
        out.append(value);
    }
    return true;
}

bool readAccessorIndices(const QVector<QByteArray> &buffers,
                         const QVector<GltfBufferView> &bufferViews,
                         const QVector<GltfAccessor> &accessors,
                         int accessorIndex,
                         QVector<unsigned int> &out,
                         QString *errorOut)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF accessor index out of range"));
        return false;
    }
    const GltfAccessor &accessor = accessors.at(accessorIndex);
    if (accessor.type != QLatin1String("SCALAR")) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Indices accessor must be SCALAR"));
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= bufferViews.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor missing bufferView"));
        return false;
    }
    if (accessor.componentType != 5121 && accessor.componentType != 5123 && accessor.componentType != 5125) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Unsupported index component type"));
        return false;
    }
    const GltfBufferView &view = bufferViews.at(accessor.bufferView);
    if (view.buffer < 0 || view.buffer >= buffers.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("BufferView references invalid buffer"));
        return false;
    }
    const QByteArray &buffer = buffers.at(view.buffer);
    const int componentSize = gltfComponentSize(accessor.componentType);
    const int stride = view.byteStride > 0 ? view.byteStride : componentSize;
    const int required = accessor.byteOffset + view.byteOffset + stride * accessor.count;
    if (required > buffer.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Accessor exceeds buffer bounds"));
        return false;
    }
    out.reserve(out.size() + accessor.count);
    const uchar *data = reinterpret_cast<const uchar *>(buffer.constData()) + view.byteOffset + accessor.byteOffset;
    for (int i = 0; i < accessor.count; ++i) {
        const uchar *ptr = data + stride * i;
        out.append(readIndexValue(ptr, accessor.componentType));
    }
    return true;
}

QString suffixForMime(const QString &mimeType)
{
    if (mimeType.compare(QStringLiteral("image/png"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".png");
    if (mimeType.compare(QStringLiteral("image/jpeg"), Qt::CaseInsensitive) == 0 ||
        mimeType.compare(QStringLiteral("image/jpg"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".jpg");
    if (mimeType.compare(QStringLiteral("image/webp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".webp");
    if (mimeType.compare(QStringLiteral("image/bmp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".bmp");
    return QStringLiteral(".bin");
}

bool writeTempTextureFile(const QByteArray &data,
                          const QString &mimeType,
                          QUrl &outUrl,
                          QString *errorOut)
{
    const QString suffix = suffixForMime(mimeType);
    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/Quick3DViewer_texXXXXXX") + suffix);
    temp.setAutoRemove(false);
    if (!temp.open()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to create temporary texture file"));
        return false;
    }
    if (temp.write(data) != data.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to write texture data"));
        return false;
    }
    temp.close();
    outUrl = QUrl::fromLocalFile(temp.fileName());
    return true;
}

bool resolveImageDataUri(const QString &uri,
                         QUrl &textureUrl,
                         QString *errorOut)
{
    QByteArray imageBytes;
    QString mime;
    if (!decodeDataUri(uri, imageBytes, errorOut, &mime))
        return false;
    if (mime.isEmpty())
        mime = QStringLiteral("application/octet-stream");
    return writeTempTextureFile(imageBytes, mime, textureUrl, errorOut);
}

bool resolveGltfImage(const QJsonObject &imageObj,
                      const QString &basePath,
                      const QVector<QByteArray> &buffers,
                      const QVector<GltfBufferView> &bufferViews,
                      QUrl &textureUrl,
                      QString *errorOut)
{
    if (imageObj.contains(QStringLiteral("uri"))) {
        const QString uri = imageObj.value(QStringLiteral("uri")).toString();
        if (uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
            return resolveImageDataUri(uri, textureUrl, errorOut);
        const QFileInfo info(uri);
        const QString resolved = info.isAbsolute() ? uri : QDir(basePath).absoluteFilePath(uri);
        if (!QFile::exists(resolved)) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Texture file %1 not found").arg(resolved));
            return false;
        }
        textureUrl = QUrl::fromLocalFile(resolved);
        return true;
    }

    const int bufferViewIndex = imageObj.value(QStringLiteral("bufferView")).toInt(-1);
    if (bufferViewIndex < 0 || bufferViewIndex >= bufferViews.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF image references invalid bufferView"));
        return false;
    }
    const QString mimeType = imageObj.value(QStringLiteral("mimeType")).toString(QStringLiteral("application/octet-stream"));
    const GltfBufferView &view = bufferViews.at(bufferViewIndex);
    if (view.buffer < 0 || view.buffer >= buffers.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Image bufferView references invalid buffer"));
        return false;
    }
    const QByteArray &buffer = buffers.at(view.buffer);
    if (view.byteOffset + view.byteLength > buffer.size()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("Image bufferView exceeds buffer size"));
        return false;
    }
    const QByteArray data = buffer.mid(view.byteOffset, view.byteLength);
    return writeTempTextureFile(data, mimeType, textureUrl, errorOut);
}

bool resolveGltfTexture(const QJsonDocument &doc,
                        const QString &basePath,
                        const QVector<QByteArray> &buffers,
                        const QVector<GltfBufferView> &bufferViews,
                        QUrl &textureUrl,
                        QString *errorOut)
{
    const QJsonArray materials = doc.object().value(QStringLiteral("materials")).toArray();
    const QJsonArray textures = doc.object().value(QStringLiteral("textures")).toArray();
    const QJsonArray images = doc.object().value(QStringLiteral("images")).toArray();
    for (const QJsonValue &matValue : materials) {
        const QJsonObject matObj = matValue.toObject();
        const QJsonObject pbr = matObj.value(QStringLiteral("pbrMetallicRoughness")).toObject();
        const QJsonObject baseTex = pbr.value(QStringLiteral("baseColorTexture")).toObject();
        if (baseTex.isEmpty())
            continue;
        const int textureIndex = baseTex.value(QStringLiteral("index")).toInt(-1);
        if (textureIndex < 0 || textureIndex >= textures.size())
            continue;
        const QJsonObject texObj = textures.at(textureIndex).toObject();
        const int imageIndex = texObj.value(QStringLiteral("source")).toInt(-1);
        if (imageIndex < 0 || imageIndex >= images.size())
            continue;
        if (!resolveGltfImage(images.at(imageIndex).toObject(),
                              basePath,
                              buffers,
                              bufferViews,
                              textureUrl,
                              errorOut)) {
            return false;
        }
        return true;
    }

    textureUrl = QUrl();
    return true;
}

bool buildGltfGeometry(const QJsonDocument &doc,
                       const QVector<QByteArray> &buffers,
                       const QString &basePath,
                       MeshImportResult &result,
                       QString *errorOut)
{
    QVector<GltfBufferView> bufferViews;
    QVector<GltfAccessor> accessors;
    if (!extractGltfAccessors(doc, bufferViews, accessors, errorOut))
        return false;

    const QJsonArray meshes = doc.object().value(QStringLiteral("meshes")).toArray();
    if (meshes.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF contains no meshes"));
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;
    QVector<unsigned int> indices;

    for (const QJsonValue &meshValue : meshes) {
        const QJsonObject meshObj = meshValue.toObject();
        const QJsonArray primitives = meshObj.value(QStringLiteral("primitives")).toArray();
        for (const QJsonValue &primValue : primitives) {
            const QJsonObject primObj = primValue.toObject();
            const int mode = primObj.value(QStringLiteral("mode")).toInt(4);
            if (mode != 4)
                continue;
            const QJsonObject attrs = primObj.value(QStringLiteral("attributes")).toObject();
            if (!attrs.contains(QStringLiteral("POSITION")))
                continue;
            QVector<QVector3D> primPositions;
            if (!readAccessorVec3(buffers,
                                  bufferViews,
                                  accessors,
                                  attrs.value(QStringLiteral("POSITION")).toInt(),
                                  primPositions,
                                  errorOut)) {
                return false;
            }
            QVector<QVector3D> primNormals;
            if (attrs.contains(QStringLiteral("NORMAL"))) {
                if (!readAccessorVec3(buffers,
                                      bufferViews,
                                      accessors,
                                      attrs.value(QStringLiteral("NORMAL")).toInt(),
                                      primNormals,
                                      errorOut)) {
                    return false;
                }
            }
            QVector<QVector2D> primTexcoords;
            if (attrs.contains(QStringLiteral("TEXCOORD_0"))) {
                if (!readAccessorVec2(buffers,
                                      bufferViews,
                                      accessors,
                                      attrs.value(QStringLiteral("TEXCOORD_0")).toInt(),
                                      primTexcoords,
                                      errorOut)) {
                    return false;
                }
            }
            QVector<unsigned int> primIndices;
            if (primObj.contains(QStringLiteral("indices"))) {
                if (!readAccessorIndices(buffers,
                                         bufferViews,
                                         accessors,
                                         primObj.value(QStringLiteral("indices")).toInt(),
                                         primIndices,
                                         errorOut)) {
                    return false;
                }
            } else {
                primIndices.reserve(primPositions.size());
                for (int i = 0; i < primPositions.size(); ++i)
                    primIndices.append(i);
            }
            if (primIndices.size() % 3 != 0) {
                MeshImportUtils::assignError(errorOut, QStringLiteral("glTF primitive does not provide triangle indices"));
                return false;
            }
            const int baseIndex = positions.size();
            positions.reserve(positions.size() + primPositions.size());
            for (const auto &pos : std::as_const(primPositions))
                positions.append(pos);
            if (!primNormals.isEmpty()) {
                normals.reserve(normals.size() + primNormals.size());
                for (const auto &n : std::as_const(primNormals))
                    normals.append(n);
            } else {
                normals.resize(positions.size());
            }
            if (!primTexcoords.isEmpty()) {
                texCoords.reserve(texCoords.size() + primTexcoords.size());
                for (const auto &uv : std::as_const(primTexcoords))
                    texCoords.append(uv);
            } else {
                texCoords.resize(positions.size());
            }
            for (unsigned int idx : std::as_const(primIndices))
                indices.append(baseIndex + idx);
        }
    }

    if (positions.isEmpty() || indices.isEmpty()) {
        MeshImportUtils::assignError(errorOut, QStringLiteral("glTF mesh has no triangles"));
        return false;
    }

    if (normals.size() != positions.size())
        normals.resize(positions.size());
    if (texCoords.size() != positions.size())
        texCoords.resize(positions.size());

    result.positions = std::move(positions);
    result.normals = std::move(normals);
    result.texCoords = std::move(texCoords);
    result.indices = std::move(indices);

    QUrl textureUrl;
    if (!resolveGltfTexture(doc, basePath, buffers, bufferViews, textureUrl, errorOut))
        return false;
    result.texture = textureUrl;
    return true;
}

bool loadGltfFile(const QString &filePath,
                  MeshImportResult &result,
                  QString *errorOut)
{
    QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    QJsonDocument doc;
    QVector<QByteArray> glbChunks;
    if (suffix == QLatin1String("glb")) {
        if (!parseGlbFile(filePath, doc, glbChunks, errorOut))
            return false;
    } else {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Cannot open %1").arg(filePath));
            return false;
        }
        QJsonParseError err;
        doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            MeshImportUtils::assignError(errorOut, QStringLiteral("Failed to parse glTF JSON: %1").arg(err.errorString()));
            return false;
        }
    }

    QVector<QByteArray> buffers;
    const QString basePath = info.dir().absolutePath();
    if (!loadGltfBuffers(doc, glbChunks, basePath, buffers, errorOut))
        return false;

    return buildGltfGeometry(doc, buffers, basePath, result, errorOut);
}

} // namespace

bool GltfMeshImporter::canLoad(const QString &suffix) const
{
    return suffix == QLatin1String("gltf") || suffix == QLatin1String("glb");
}

bool GltfMeshImporter::load(const QString &filePath,
                            MeshImportResult &result,
                            QString *errorOut) const
{
    return loadGltfFile(filePath, result, errorOut);
}
