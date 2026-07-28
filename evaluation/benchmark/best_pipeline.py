#!/usr/bin/env python3
"""Measure every stage of the selected end-to-end RAG configuration."""
from __future__ import annotations
import json, math, os, statistics, time
from pathlib import Path
import requests

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"evaluation/benchmark/results"
MODEL="qwen3.7-text-embedding"; DIM=1024; RERANK="qwen3-rerank"; LLM="qwen-plus"

def pct(v,q):
    v=sorted(v);return v[min(len(v)-1,math.ceil(len(v)*q)-1)]
def cosine(a,b):
    return sum(x*y for x,y in zip(a,b))/(math.sqrt(sum(x*x for x in a))*math.sqrt(sum(x*x for x in b))+1e-12)

class API:
    def __init__(self):
        self.s=requests.Session();self.s.trust_env=False
        self.key=os.environ["BAILIAN_API_KEY"];self.base=os.environ["BAILIAN_BASE_URL"].rstrip("/")
        self.rerank=os.getenv("RAG_RERANK_URL","https://dashscope.aliyuncs.com/api/v1/services/rerank/text-rerank/text-rerank")
    def embed(self,texts):
        t=time.perf_counter();r=self.s.post(self.base+"/embeddings",
          headers={"Authorization":"Bearer "+self.key},
          json={"model":MODEL,"input":texts,"dimensions":DIM,"encoding_format":"float"},timeout=60)
        r.raise_for_status();return [x["embedding"] for x in r.json()["data"]],(time.perf_counter()-t)*1000
    def rerank_docs(self,q,docs):
        t=time.perf_counter();r=self.s.post(self.rerank,headers={"Authorization":"Bearer "+self.key},
          json={"model":RERANK,"input":{"query":q,"documents":docs},
                "parameters":{"top_n":min(5,len(docs)),"return_documents":False}},timeout=60)
        r.raise_for_status();return r.json()["output"]["results"],(time.perf_counter()-t)*1000
    def answer(self,prompt):
        t=time.perf_counter();r=self.s.post(self.base+"/chat/completions",
          headers={"Authorization":"Bearer "+self.key},
          json={"model":LLM,"messages":[
            {"role":"system","content":"只能根据给定资料回答；资料没有答案时必须明确说无法根据资料回答，不得猜测。"},
            {"role":"user","content":prompt}],"temperature":0.2,"max_tokens":800,
                "stream":True,"stream_options":{"include_usage":True}},timeout=90,stream=True)
        r.raise_for_status()
        pieces=[];ttft_ms=None
        for raw in r.iter_lines(decode_unicode=True):
            if not raw or not raw.startswith("data:"):continue
            data=raw[5:].strip()
            if not data or data=="[DONE]":continue
            event=json.loads(data)
            for choice in event.get("choices",[]):
                content=choice.get("delta",{}).get("content","")
                if content:
                    if ttft_ms is None:ttft_ms=(time.perf_counter()-t)*1000
                    pieces.append(content)
        total_ms=(time.perf_counter()-t)*1000
        return "".join(pieces),ttft_ms or total_ms,total_ms

def main():
    api=API()
    docs=[]
    for p in sorted((ROOT/"knowledge/documents").glob("*")):
        docs.append({"id":p.name,"text":p.read_text(errors="replace")})
    doc_vecs,index_ms=api.embed([d["text"] for d in docs])
    cases=json.loads((ROOT/"evaluation/ragas/dataset/golden.json").read_text())
    rows=[];collected=[]
    for case in cases:
        q=case["question"]
        qvecs,embed_ms=api.embed([q]);qv=qvecs[0]
        t=time.perf_counter()
        initial=sorted(range(len(docs)),key=lambda i:cosine(qv,doc_vecs[i]),reverse=True)
        scores=[cosine(qv,doc_vecs[i]) for i in initial]
        vector_ms=(time.perf_counter()-t)*1000
        ranked,rerank_ms=api.rerank_docs(q,[docs[i]["text"] for i in initial])
        order=[initial[x["index"]] for x in ranked]
        t=time.perf_counter()
        contexts=[docs[i]["text"] for i in order]
        prompt="问题："+q+"\n\n资料：\n"+"\n\n".join(
            f"[来源{n+1}] {text}" for n,text in enumerate(contexts))
        prompt_ms=(time.perf_counter()-t)*1000
        answer,llm_ttft_ms,llm_ms=api.answer(prompt)
        target={"reactor":"reactor.md","security":"security.md","pool":"thread_pool.txt"}.get(case["id"].split("-")[0])
        ranks=[i+1 for i,idx in enumerate(order) if docs[idx]["id"]==target] if target else []
        rows.append({"id":case["id"],"embedding_ms":embed_ms,"vector_ms":vector_ms,
                     "rerank_ms":rerank_ms,"prompt_ms":prompt_ms,"llm_ms":llm_ms,
                     "llm_ttft_ms":llm_ttft_ms,
                     "end_to_end_ttft_ms":embed_ms+vector_ms+rerank_ms+prompt_ms+llm_ttft_ms,
                     "total_ms":embed_ms+vector_ms+rerank_ms+prompt_ms+llm_ms,
                     "target_rank":ranks[0] if ranks else None,"top_similarity":scores[0]})
        collected.append({**case,"response":answer,"retrieved_contexts":contexts,
                          "retrieved_context_ids":[f"knowledge:{docs[i]['id']}" for i in order],
                          "source_types":["knowledge"]*len(contexts),"grounded":True,
                          "latency_ms":rows[-1]["total_ms"],"request_id":"best-pipeline",
                          "collection_error":None})
        print(case["id"],round(rows[-1]["total_ms"],1),flush=True)
    stages=["embedding_ms","vector_ms","rerank_ms","prompt_ms","llm_ttft_ms",
            "end_to_end_ttft_ms","llm_ms","total_ms"]
    answerable=[r for r in rows if r["target_rank"]]
    summary={"configuration":{"chunking":"semantic","embedding":MODEL,"dimension":DIM,
                              "rerank":RERANK,"llm":LLM},
             "samples":len(rows),"index_embedding_ms":index_ms,
             "recall@1":statistics.mean(r["target_rank"]<=1 for r in answerable),
             "recall@3":statistics.mean(r["target_rank"]<=3 for r in answerable),
             "mrr":statistics.mean(1/r["target_rank"] for r in answerable),
             "latency":{s:{"mean":statistics.mean(r[s] for r in rows),
                           "p50":pct([r[s] for r in rows],.5),
                           "p95":pct([r[s] for r in rows],.95),
                           "p99":pct([r[s] for r in rows],.99)} for s in stages},
             "rows":rows}
    OUT.mkdir(parents=True,exist_ok=True)
    stamp=time.strftime("%Y%m%d-%H%M%S")
    result=OUT/f"best-pipeline-{stamp}.json";result.write_text(json.dumps(summary,ensure_ascii=False,indent=2))
    collected_path=ROOT/"evaluation/ragas/dataset/collected.best.json"
    collected_path.write_text(json.dumps(collected,ensure_ascii=False,indent=2))
    print(result);print(collected_path)
if __name__=="__main__":main()
