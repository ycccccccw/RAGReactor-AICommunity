#!/usr/bin/env python3
"""Integration fault injection for rate limit, timeout and circuit recovery."""
from __future__ import annotations
import json, os, signal, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import requests

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"evaluation/benchmark/results"
state={"embedding_calls":0,"fail_first":2,"delay":0.0}

class Mock(BaseHTTPRequestHandler):
    def log_message(self,*_): pass
    def do_POST(self):
        length=int(self.headers.get("Content-Length","0")); self.rfile.read(length)
        if self.path.endswith("/embeddings"):
            state["embedding_calls"]+=1
            if state["delay"]: time.sleep(state["delay"])
            if state["embedding_calls"]<=state["fail_first"]:
                self.send_response(500);self.end_headers();self.wfile.write(b'{"error":{"message":"injected"}}');return
            body={"data":[{"embedding":[0.03125]*1024}]}
        elif self.path.endswith("/chat/completions"):
            body={"choices":[{"message":{"content":"线程池复用工作线程，避免重复创建和销毁线程。"}}]}
        else:
            self.send_response(404);self.end_headers();return
        raw=json.dumps(body).encode();self.send_response(200)
        self.send_header("Content-Type","application/json");self.send_header("Content-Length",str(len(raw)))
        self.end_headers()
        try:self.wfile.write(raw)
        except BrokenPipeError:pass

def start_server(extra):
    env=os.environ.copy(); env.update({
        "BAILIAN_API_KEY":"mock-key","BAILIAN_BASE_URL":"http://127.0.0.1:19090/v1",
        "RAG_RERANK_ENABLED":"false","RAG_CACHE_CAPACITY":"0",
        "RAG_CIRCUIT_FAILURE_THRESHOLD":"2","RAG_CIRCUIT_COOLDOWN_SECONDS":"2",
        "RAG_RELEVANCE_THRESHOLD":"-1",
    });env.update(extra)
    for k in ["HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"]:env.pop(k,None)
    p=subprocess.Popen([str(ROOT/"server"),"-p","9006","-r","2","-t","4","-s","8","-c","1"],
                       cwd=ROOT,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT)
    s=requests.Session();s.trust_env=False
    for _ in range(50):
        try:
            if s.get("http://127.0.0.1:9006/api/health",timeout=.3).ok:return p,s
        except requests.RequestException:pass
        time.sleep(.1)
    raise RuntimeError("server start failed")

def login(s):
    s.post("http://127.0.0.1:9006/2CGISQL.cgi",data={"user":"test","password":"123456"},timeout=5)

def ask(s,n):
    t=time.perf_counter()
    r=s.post("http://127.0.0.1:9006/api/ask",
             headers={"X-CSRF-Token":s.cookies["csrf_token"]},
             json={"question":f"线程池有什么作用？{n}","stream":False},timeout=10)
    return {"status":r.status_code,"latency_ms":round((time.perf_counter()-t)*1000,2),
            "code":(r.json().get("error",{}).get("code") if r.headers.get("Content-Type","").startswith("application/json") else "")}

def stop(p):
    p.send_signal(signal.SIGTERM)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.kill();p.wait()

def main():
    mock=ThreadingHTTPServer(("127.0.0.1",19090),Mock)
    threading.Thread(target=mock.serve_forever,daemon=True).start()
    state.update(embedding_calls=0,fail_first=2,delay=0)
    p,s=start_server({"RAG_EMBEDDING_TIMEOUT_MS":"1000"});login(s)
    circuit=[ask(s,i) for i in range(3)];time.sleep(2.2);circuit.append(ask(s,4));stop(p)
    state.update(embedding_calls=0,fail_first=0,delay=.35)
    p,s=start_server({"RAG_EMBEDDING_TIMEOUT_MS":"100"});login(s)
    timeout=[ask(s,i) for i in range(2)];stop(p);mock.shutdown()
    result={"circuit_sequence":circuit,"circuit_recovered":circuit[-1]["status"]==200,
            "timeout_requests":timeout,
            "timeout_rate":sum(x["status"]==502 for x in timeout)/len(timeout)}
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/f"resilience-{time.strftime('%Y%m%d-%H%M%S')}.json"
    path.write_text(json.dumps(result,ensure_ascii=False,indent=2));print(path);print(json.dumps(result,ensure_ascii=False))
if __name__=="__main__":main()
