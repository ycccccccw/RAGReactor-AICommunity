#!/usr/bin/env python3
"""Controlled retrieval benchmark for chunking, embeddings and rerankers."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evaluation/benchmark/results"
CACHE = ROOT / "evaluation/benchmark/cache"
HISTORICAL_DOCS = [
    "docs/AI_RAG_COMMUNITY_INTEGRATION_PLAN.md",
    "docs/STAGE0_BASELINE_REPORT.md",
    "docs/STAGE1_DYNAMIC_FEED_GUIDE.md",
    "docs/STAGE2_UNIFIED_INDEX_GUIDE.md",
    "docs/STAGE3_RECOMMENDATION_GUIDE.md",
    "docs/STAGE4_RAG_COMMUNITY_FUSION_GUIDE.md",
    "docs/STAGE5_FULL_SYSTEM_ACCEPTANCE_REPORT.md",
    "docs/community-api-contract.md",
    "docs/DEPLOYMENT_AND_ROLLBACK.md",
    "docs/WEBSERVER_REGRESSION_FIX.md",
]


@dataclass
class Chunk:
    id: str
    doc_id: str
    text: str
    parent: str = ""


class Models:
    def __init__(self):
        self.s = requests.Session()
        self.s.trust_env = False
        self.key = os.environ["BAILIAN_API_KEY"]
        self.base = os.environ["BAILIAN_BASE_URL"].rstrip("/")
        self.rerank_url = os.getenv(
            "RAG_RERANK_URL",
            "https://dashscope.aliyuncs.com/api/v1/services/rerank/text-rerank/text-rerank",
        )

    def embeddings(self, texts: list[str], model: str, dim: int) -> tuple[list[list[float]], float]:
        key = hashlib.sha256(json.dumps([model, dim, texts], ensure_ascii=False).encode()).hexdigest()
        path = CACHE / f"emb-{key}.json"
        if path.exists():
            return json.loads(path.read_text()), 0.0
        started = time.perf_counter()
        vectors = []
        batch = 10
        for i in range(0, len(texts), batch):
            response = self.s.post(
                self.base + "/embeddings",
                headers={"Authorization": "Bearer " + self.key},
                json={"model": model, "input": texts[i:i + batch],
                      "dimensions": dim, "encoding_format": "float"},
                timeout=90,
            )
            response.raise_for_status()
            vectors.extend(item["embedding"] for item in response.json()["data"])
        elapsed = (time.perf_counter() - started) * 1000
        CACHE.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(vectors))
        return vectors, elapsed

    def rerank(self, query: str, docs: list[str], model: str, top_n: int) -> tuple[list[int], float]:
        if model == "none":
            return list(range(min(top_n, len(docs)))), 0.0
        started = time.perf_counter()
        response = self.s.post(
            self.rerank_url,
            headers={"Authorization": "Bearer " + self.key},
            json={"model": model, "input": {"query": query, "documents": docs},
                  "parameters": {"top_n": top_n, "return_documents": False}},
            timeout=90,
        )
        response.raise_for_status()
        return [x["index"] for x in response.json()["output"]["results"]], (time.perf_counter()-started)*1000


def historical_corpus() -> dict[str, str]:
    docs = {}
    for name in HISTORICAL_DOCS:
        result = subprocess.run(
            ["git", "show", f"035689f:{name}"], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
        if result.returncode == 0:
            docs[name] = result.stdout
    for path in sorted((ROOT / "knowledge/documents").glob("*")):
        if path.is_file():
            docs[str(path.relative_to(ROOT))] = path.read_text(errors="replace")
    return docs


def units(text: str) -> list[str]:
    return [x.strip() for x in re.split(r"(?<=[。！？.!?])\s+|\n+", text) if x.strip()]


def fixed_chunks(doc_id: str, text: str, size=500, overlap=80) -> list[Chunk]:
    result, pos = [], 0
    while pos < len(text):
        body = text[pos:pos + size].strip()
        if body:
            result.append(Chunk(f"{doc_id}#{len(result)}", doc_id, body))
        if pos + size >= len(text):
            break
        pos += size - overlap
    return result


def paragraph_chunks(doc_id: str, text: str, target=650) -> list[Chunk]:
    result, current, heading = [], [], ""
    for block in re.split(r"\n\s*\n", text):
        block = block.strip()
        if not block:
            continue
        if re.match(r"^#{1,6}\s", block):
            heading = block.splitlines()[0]
        candidate = "\n\n".join(current + ([heading] if heading and heading not in current else []) + [block])
        if current and len(candidate) > target:
            body = "\n\n".join(current)
            result.append(Chunk(f"{doc_id}#{len(result)}", doc_id, body))
            current = ([heading] if heading else []) + [block]
        else:
            current = candidate.split("\n\n")
    if current:
        result.append(Chunk(f"{doc_id}#{len(result)}", doc_id, "\n\n".join(current)))
    return result


def semantic_chunks(doc_id: str, text: str, vectors: list[list[float]], start: int) -> tuple[list[Chunk], int]:
    sentences = units(text)
    if not sentences:
        return [], start
    result, group = [], [sentences[0]]
    for i in range(1, len(sentences)):
        a, b = vectors[start+i-1], vectors[start+i]
        sim = sum(x*y for x, y in zip(a, b)) / (
            math.sqrt(sum(x*x for x in a))*math.sqrt(sum(x*x for x in b)) + 1e-12)
        if (sim < 0.58 and len("".join(group)) >= 180) or len("".join(group)) >= 700:
            result.append(Chunk(f"{doc_id}#{len(result)}", doc_id, "\n".join(group)))
            group = []
        group.append(sentences[i])
    if group:
        result.append(Chunk(f"{doc_id}#{len(result)}", doc_id, "\n".join(group)))
    return result, start + len(sentences)


def parent_child_chunks(doc_id: str, text: str) -> list[Chunk]:
    result = []
    for p, parent in enumerate(paragraph_chunks(doc_id, text, 1200)):
        children = fixed_chunks(doc_id, parent.text, 260, 40)
        for child in children:
            result.append(Chunk(f"{doc_id}#p{p}c{len(result)}", doc_id, child.text, parent.text))
    return result


def cosine(a, b):
    return sum(x*y for x, y in zip(a, b)) / (
        math.sqrt(sum(x*x for x in a))*math.sqrt(sum(x*x for x in b)) + 1e-12)


def percentile(values, q):
    values = sorted(values)
    return values[min(len(values)-1, math.ceil(q*len(values))-1)] if values else 0


def evaluate(chunks, cases, models, emb_model, dim, reranker):
    chunk_vecs, indexing_ms = models.embeddings([c.text for c in chunks], emb_model, dim)
    query_vecs, query_batch_ms = models.embeddings([c["question"] for c in cases], emb_model, dim)
    recalls = {1: [], 3: [], 5: [], 10: []}
    reciprocal, query_lat, rerank_lat = [], [], []
    for case, qv in zip(cases, query_vecs):
        started = time.perf_counter()
        initial = sorted(range(len(chunks)), key=lambda i: cosine(qv, chunk_vecs[i]), reverse=True)[:20]
        order, rr_ms = models.rerank(case["question"], [chunks[i].text for i in initial], reranker, 10)
        ranked = [initial[i] for i in order]
        query_lat.append((time.perf_counter()-started)*1000)
        rerank_lat.append(rr_ms)
        relevant = set(case["document_ids"])
        ranks = [i+1 for i, idx in enumerate(ranked) if chunks[idx].doc_id in relevant]
        reciprocal.append(1/min(ranks) if ranks else 0)
        for k in recalls:
            recalls[k].append(float(any(chunks[idx].doc_id in relevant for idx in ranked[:k])))
    return {
        "chunk_count": len(chunks), "index_embedding_ms": indexing_ms,
        "query_embedding_batch_ms": query_batch_ms,
        **{f"recall@{k}": statistics.mean(v) for k, v in recalls.items()},
        "mrr": statistics.mean(reciprocal),
        "retrieval_p50_ms": percentile(query_lat, .50),
        "retrieval_p95_ms": percentile(query_lat, .95),
        "rerank_mean_ms": statistics.mean(rerank_lat),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", default="evaluation/benchmark/dataset.json")
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    docs = historical_corpus()
    cases = json.loads((ROOT / args.dataset).read_text())
    models = Models()
    CACHE.mkdir(parents=True, exist_ok=True)
    sentence_texts = [s for text in docs.values() for s in units(text)]
    sem_vecs, _ = models.embeddings(sentence_texts, "text-embedding-v4", 512)
    strategies = {}
    strategies["fixed"] = [c for d,t in docs.items() for c in fixed_chunks(d,t)]
    strategies["paragraph"] = [c for d,t in docs.items() for c in paragraph_chunks(d,t)]
    strategies["parent_child"] = [c for d,t in docs.items() for c in parent_child_chunks(d,t)]
    semantic, offset = [], 0
    for d,t in docs.items():
        made, offset = semantic_chunks(d,t,sem_vecs,offset)
        semantic.extend(made)
    strategies["semantic"] = semantic
    candidates = [("text-embedding-v3",512), ("text-embedding-v4",512),
                  ("text-embedding-v4",1024), ("text-embedding-v4",2048),
                  ("qwen3.7-text-embedding",1024)]
    results = []
    for strategy, chunks in strategies.items():
        results.append({"phase":"chunking","strategy":strategy,"embedding":"text-embedding-v4",
                        "dimension":1024,"reranker":"none",
                        **evaluate(chunks,cases,models,"text-embedding-v4",1024,"none")})
    best = max(results, key=lambda x:(x["recall@5"],x["mrr"]))["strategy"]
    for model, dim in candidates:
        results.append({"phase":"embedding","strategy":best,"embedding":model,"dimension":dim,
                        "reranker":"none",**evaluate(strategies[best],cases,models,model,dim,"none")})
    best_emb = max((r for r in results if r["phase"]=="embedding"),
                   key=lambda x:(x["recall@5"],x["mrr"],-x["retrieval_p95_ms"]))
    for reranker in ["none","qwen3-rerank","gte-rerank-v2"]:
        results.append({"phase":"rerank","strategy":best,"embedding":best_emb["embedding"],
                        "dimension":best_emb["dimension"],"reranker":reranker,
                        **evaluate(strategies[best],cases,models,best_emb["embedding"],
                                   best_emb["dimension"],reranker)})
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"retrieval-{time.strftime('%Y%m%d-%H%M%S')}.json"
    path.write_text(json.dumps({"corpus_documents":len(docs),"test_cases":len(cases),
                                "results":results},ensure_ascii=False,indent=2))
    print(path)


if __name__ == "__main__":
    main()
