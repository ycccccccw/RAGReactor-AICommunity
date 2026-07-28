#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from ragas_eval.client import RAGReactorClient
from ragas_eval.settings import env_required, load_settings, project_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Collect RAGReactor answers for RAGAS")
    parser.add_argument("--config", help="YAML configuration path")
    parser.add_argument("--dataset", help="Override golden dataset path")
    parser.add_argument("--output", help="Override collected output path")
    parser.add_argument("--limit", type=int, help="Collect only the first N cases")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    settings = load_settings(args.config)
    server = settings["server"]
    paths = settings["paths"]
    dataset_path = project_path(args.dataset or paths["dataset"])
    output_path = project_path(args.output or paths["collected"])
    cases = json.loads(dataset_path.read_text(encoding="utf-8"))
    if not isinstance(cases, list) or not cases:
        raise RuntimeError(
            f"dataset is empty: {dataset_path}; copy and edit golden.example.json first"
        )
    if args.limit is not None:
        cases = cases[: args.limit]

    username = env_required(server["username_env"])
    password = env_required(server["password_env"])
    client = RAGReactorClient(
        server["base_url"],
        float(server["connect_timeout_seconds"]),
        float(server["answer_timeout_seconds"]),
    )
    client.login(username, password)
    health = client.health()
    if not health.get("rag_configured"):
        raise RuntimeError("RAG service is not configured")

    collected: list[dict] = []
    failures = 0
    for index, case in enumerate(cases, start=1):
        case_id = str(case.get("id") or f"case-{index:03d}")
        question = str(case.get("question", "")).strip()
        if not question:
            raise RuntimeError(f"{case_id}: question is empty")
        print(f"[{index}/{len(cases)}] {case_id}: {question}", flush=True)
        record = dict(case)
        try:
            answer = client.ask(
                question,
                int(case.get("top_k", server["top_k"])),
                bool(case.get("include_community", False)),
            )
            record.update(
                {
                    "response": answer.answer,
                    "retrieved_contexts": answer.contexts,
                    "retrieved_context_ids": answer.context_ids,
                    "source_types": answer.source_types,
                    "grounded": answer.grounded,
                    "latency_ms": round(answer.latency_ms, 3),
                    "request_id": answer.request_id,
                    "collection_error": None,
                }
            )
        except Exception as exc:
            failures += 1
            record.update(
                {
                    "response": "",
                    "retrieved_contexts": [],
                    "retrieved_context_ids": [],
                    "source_types": [],
                    "grounded": False,
                    "collection_error": str(exc),
                }
            )
        collected.append(record)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(collected, ensure_ascii=False, indent=2), encoding="utf-8"
        )

    print(f"collected={len(collected)} failures={failures} output={output_path}")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"collection failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
