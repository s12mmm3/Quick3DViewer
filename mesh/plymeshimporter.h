#ifndef PLYMESHIMPORTER_H
#define PLYMESHIMPORTER_H

#include "meshimporthelper.h"

class PlyMeshImporter : public AbstractMeshImporter
{
public:
    bool canLoad(const QString &suffix) const override;
    bool load(const QString &filePath,
              MeshImportResult &result,
              QString *errorOut) const override;
};

#endif // PLYMESHIMPORTER_H
