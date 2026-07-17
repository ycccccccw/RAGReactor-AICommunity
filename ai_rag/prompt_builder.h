#ifndef RAGREACTOR_PROMPT_BUILDER_H
#define RAGREACTOR_PROMPT_BUILDER_H

#include "rag_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
class PromptBuilder
{
public:
    explicit PromptBuilder(std::size_t max_bytes = 12000);

    std::string build(const std::string &question,
                      const std::vector<SearchResult> &results) const;

private:
    std::size_t max_bytes_;
};
}

#endif
