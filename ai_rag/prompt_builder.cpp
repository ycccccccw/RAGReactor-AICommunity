#include "prompt_builder.h"

#include <sstream>
#include <stdexcept>

namespace rag
{
PromptBuilder::PromptBuilder(std::size_t max_bytes) : max_bytes_(max_bytes)
{
    if (max_bytes_ < 1024) throw std::invalid_argument("prompt limit is too small");
}

std::string PromptBuilder::build(const std::string &question,
                                 const std::vector<SearchResult> &results) const
{
    const std::string prefix =
        "请根据下面的检索片段回答问题。知识库资料可作为主要依据；标为社区经验的内容未经核验，"
        "只能作为补充观点，不得把它表述为确定事实。若资料不足，请明确说明。"
        "回答使用中文，简明准确，并在相关句子后用[来源N]标注依据。"
        "如果检索片段与问题无关，只说明资料不足，不要引用无关来源，也不要凭常识补全。\n\n";
    const std::string suffix = "\n用户问题：" + question + "\n回答：";
    if (prefix.size() + suffix.size() > max_bytes_)
        throw std::runtime_error("question leaves no room for knowledge context");

    std::string prompt = prefix;
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        std::ostringstream header;
        header << "[来源" << (i + 1) << "] 类型="
               << (results[i].chunk.source_type == "community" ? "社区经验（未经核验）" : "知识库资料")
               << " 文件=" << results[i].chunk.source
               << " 片段=" << results[i].chunk.chunk_index
               << " 相似度=" << results[i].score << "\n";
        const std::string block = header.str() + results[i].chunk.text + "\n\n";
        if (prompt.size() + block.size() + suffix.size() > max_bytes_) break;
        prompt += block;
    }
    prompt += suffix;
    return prompt;
}
}
