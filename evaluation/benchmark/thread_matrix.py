#!/usr/bin/env python3
"""Start only owned server processes and benchmark Sub Reactor/worker matrices."""
from __future__ import annotations
import argparse, json, os, re, signal, subprocess, tempfile, time
from pathlib import Path
import requests

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/"evaluation/benchmark/results"

def ms(value, unit):
    return float(value)*({"us":.001,"ms":1,"s":1000}[unit])

def parse_wrk(text):
    lat = re.search(r"Latency\s+([\d.]+)(us|ms|s).*?([\d.]+)(us|ms|s).*?([\d.]+)(us|ms|s)",text)
    qps = re.search(r"Requests/sec:\s+([\d.]+)",text)
    total = re.search(r"(\d+) requests in",text)
    errors = sum(map(int,re.findall(r"(?:Connect|Read|Write|Timeout) errors:\s*(\d+)",text)))
    non2xx = re.search(r"Non-2xx or 3xx responses:\s+(\d+)",text)
    p99 = re.search(r"99%\s+([\d.]+)(us|ms|s)", text)
    return {"qps":float(qps.group(1)) if qps else 0,
            "latency_mean_ms":ms(lat.group(1),lat.group(2)) if lat else 0,
            "p99_ms":ms(p99.group(1),p99.group(2)) if p99 else 0,
            "latency_max_ms":ms(lat.group(5),lat.group(6)) if lat else 0,
            "requests":int(total.group(1)) if total else 0,
            "socket_errors":errors,
            "non_2xx":int(non2xx.group(1)) if non2xx else 0}

def percentile(values,q):
    values=sorted(values)
    return values[min(len(values)-1,int(len(values)*q))] if values else 0

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--duration",default="10s"); ap.add_argument("--connections",type=int,default=32)
    ap.add_argument("--username",default=os.getenv("RAGAS_TEST_USERNAME","test"))
    ap.add_argument("--password",default=os.getenv("RAGAS_TEST_PASSWORD","123456"))
    args=ap.parse_args()
    matrix=[(1,2),(1,4),(2,2),(2,4),(2,8),(4,4),(4,8),(4,16)]
    env=os.environ.copy()
    for k in ["HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"]: env.pop(k,None)
    results=[]
    for reactors,workers in matrix:
        log=tempfile.NamedTemporaryFile(prefix="ragreactor-",suffix=".log",delete=False)
        process=subprocess.Popen([str(ROOT/"server"),"-p","9006","-r",str(reactors),"-t",str(workers),
                                  "-s","8","-c","1"],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            session=requests.Session();session.trust_env=False
            for _ in range(50):
                try:
                    if session.get("http://127.0.0.1:9006/api/health",timeout=.5).ok: break
                except requests.RequestException: pass
                time.sleep(.1)
            else: raise RuntimeError(f"server did not start; log={log.name}")
            login=session.post("http://127.0.0.1:9006/2CGISQL.cgi",
                               data={"user":args.username,"password":args.password},timeout=5)
            if not session.cookies.get("sid"): raise RuntimeError("test login failed")
            cookie="; ".join(f"{k}={v}" for k,v in session.cookies.items())
            # During concurrent Feed traffic, sample real login latency.
            load=subprocess.Popen(["wrk","-t","2","-c",str(args.connections),"-d",args.duration,
                                   "-H","Cookie: "+cookie,"--latency",
                                   "http://127.0.0.1:9006/api/community/feed?limit=10"],
                                  text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            time.sleep(.5)
            login_ms=[]; login_failures=0
            for _ in range(10):
                t=time.perf_counter()
                try:
                    r=session.post("http://127.0.0.1:9006/2CGISQL.cgi",
                                   data={"user":args.username,"password":args.password},timeout=5)
                    if r.status_code>=500: login_failures+=1
                except requests.RequestException: login_failures+=1
                login_ms.append((time.perf_counter()-t)*1000)
            raw=load.communicate(timeout=60)[0]
            measured=parse_wrk(raw)
            results.append({"sub_reactors":reactors,"workers":workers,**measured,
                            "login_p99_ms":percentile(login_ms,.99),
                            "login_failures":login_failures,"raw":raw})
            print(reactors,workers,measured["qps"],measured["latency_max_ms"])
        finally:
            process.send_signal(signal.SIGTERM)
            try: process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill();process.wait()
            log.close()
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/f"threads-{time.strftime('%Y%m%d-%H%M%S')}.json"
    path.write_text(json.dumps({"cpu_count":os.cpu_count(),"connections":args.connections,
                                "duration":args.duration,"results":results},ensure_ascii=False,indent=2))
    print(path)
if __name__=="__main__": main()
