#ifndef STLMESHIMPORTER_H
#define STLMESHIMPORTER_H

#include "meshimporthelper.h"

class StlMeshImporter : public AbstractMeshImporter
{
public:
    bool canLoad(const QString &suffix) const override;
    bool load(const QString &filePath,
              MeshImportResult &result,
              QString *errorOut) const override;
};

#endif // STLMESHIMPORTER_H
