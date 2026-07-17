#include "document_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rag
{
namespace
{
std::string lower_extension(const fs::path &path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

void set_error(std::string *error, const std::string &message)
{
    if (error) *error = message;
}
}

bool DocumentLoader::is_supported(const std::string &path)
{
    const std::string extension = lower_extension(fs::path(path));
    return extension == ".md" || extension == ".txt";
}

bool DocumentLoader::load_file(const std::string &path, Document &document,
                               std::string *error)
{
    if (!is_supported(path))
    {
        set_error(error, "unsupported document type: " + path);
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        set_error(error, "failed to open document: " + path);
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        set_error(error, "failed to read document: " + path);
        return false;
    }

    const fs::path file_path(path);
    document.id = file_path.lexically_normal().generic_string();
    document.source = file_path.filename().string();
    document.content = buffer.str();
    return true;
}

bool DocumentLoader::load_directory(const std::string &directory,
                                    std::vector<Document> &documents,
                                    std::string *error)
{
    documents.clear();
    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec))
    {
        set_error(error, "document directory does not exist: " + directory);
        return false;
    }

    std::vector<fs::path> paths;
    for (fs::recursive_directory_iterator it(directory, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            set_error(error, "failed to enumerate document directory: " + ec.message());
            return false;
        }
        if (it->is_regular_file() && is_supported(it->path().string()))
            paths.push_back(it->path());
    }

    std::sort(paths.begin(), paths.end());
    for (const fs::path &path : paths)
    {
        Document document;
        if (!load_file(path.string(), document, error))
            return false;
        document.id = fs::relative(path, directory, ec).generic_string();
        if (ec) document.id = path.filename().generic_string();
        documents.push_back(std::move(document));
    }
    return true;
}
}
