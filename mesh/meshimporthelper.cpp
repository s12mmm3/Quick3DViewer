#include "meshimporthelper.h"

#include "gltfmeshimporter.h"
#include "meshimporterutils.h"
#include "objmeshimporter.h"
#include "plymeshimporter.h"
#include "stlmeshimporter.h"

#include <QFileInfo>

namespace {

const QVector<AbstractMeshImporter *> &importers()
{
    static ObjMeshImporter obj;
    static StlMeshImporter stl;
    static PlyMeshImporter ply;
    static GltfMeshImporter gltf;
    static const QVector<AbstractMeshImporter *> instances{&obj, &stl, &ply, &gltf};
    return instances;
}

} // namespace

bool MeshImportHelper::load(const QString &filePath,
                            MeshImportResult &result,
                            QString *errorOut)
{
    result = MeshImportResult();
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    for (AbstractMeshImporter *importer : importers()) {
        if (!importer->canLoad(suffix))
            continue;
        const bool ok = importer->load(filePath, result, errorOut);
        if (!ok && errorOut && errorOut->isEmpty())
            *errorOut = QStringLiteral("Failed to load mesh: %1").arg(filePath);
        return ok;
    }

    MeshImportUtils::assignError(errorOut, QStringLiteral("Unsupported extension: %1").arg(suffix));
    return false;
}
