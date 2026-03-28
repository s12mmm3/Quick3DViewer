#ifndef OBJMESHIMPORTER_H
#define OBJMESHIMPORTER_H

#include "meshimporthelper.h"

class ObjMeshImporter : public AbstractMeshImporter
{
public:
    bool canLoad(const QString &suffix) const override;
    bool load(const QString &filePath,
              MeshImportResult &result,
              QString *errorOut) const override;
};

#endif // OBJMESHIMPORTER_H
