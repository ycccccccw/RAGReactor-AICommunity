#ifndef RAGREACTOR_DOCUMENT_LOADER_H
#define RAGREACTOR_DOCUMENT_LOADER_H

#include "rag_types.h"

#include <string>
#include <vector>

namespace rag
{
class DocumentLoader
{
public:
    static bool load_file(const std::string &path, Document &document,
                          std::string *error = nullptr);
    static bool load_directory(const std::string &directory,
                               std::vector<Document> &documents,
                               std::string *error = nullptr);
    static bool is_supported(const std::string &path);
};
}

#endif
