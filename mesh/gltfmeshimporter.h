#ifndef GLTFMESHIMPORTER_H
#define GLTFMESHIMPORTER_H

#include "meshimporthelper.h"

class GltfMeshImporter : public AbstractMeshImporter
{
public:
    bool canLoad(const QString &suffix) const override;
    bool load(const QString &filePath,
              MeshImportResult &result,
              QString *errorOut) const override;
};

#endif // GLTFMESHIMPORTER_H
