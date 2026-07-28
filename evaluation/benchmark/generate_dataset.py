#!/usr/bin/env python3
"""Generate a reproducible project-domain retrieval set, then keep it for review."""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from benchmark import Models, historical_corpus

ROOT = Path(__file__).resolve().parents[2]
prompt = """你是测试集工程师。根据下面文档生成5个能由该文档直接回答的中文问题。
问题应覆盖不同细节，不能提到“本文/文档”，答案必须是文中的可验证事实。
只输出JSON数组，每项格式为{"question":"...","reference_answer":"..."}。
文档：
"""

def main():
    models = Models()
    rows = []
    for doc_id, text in historical_corpus().items():
        response = models.s.post(
            models.base + "/chat/completions",
            headers={"Authorization":"Bearer "+models.key},
            json={"model":os.getenv("RAG_LLM_MODEL","qwen-plus"),
                  "messages":[{"role":"user","content":prompt+text[:10000]}],
                  "temperature":0,"response_format":{"type":"json_object"}},
            timeout=120)
        response.raise_for_status()
        raw = response.json()["choices"][0]["message"]["content"].strip()
        parsed = json.loads(raw)
        items = parsed if isinstance(parsed,list) else next(v for v in parsed.values() if isinstance(v,list))
        for item in items[:5]:
            rows.append({**item,"document_ids":[doc_id],"include_community":False,
                         "category":"project_grounded"})
    path = ROOT/"evaluation/benchmark/dataset.json"
    path.write_text(json.dumps(rows,ensure_ascii=False,indent=2))
    print(f"{path}: {len(rows)} cases")

if __name__ == "__main__":
    main()
