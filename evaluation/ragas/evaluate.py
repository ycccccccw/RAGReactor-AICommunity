#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import math
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

import pandas as pd
from openai import AsyncOpenAI
from ragas.embeddings import OpenAIEmbeddings
from ragas.llms import llm_factory
from ragas.metrics.collections import (
    AnswerRelevancy,
    ContextPrecision,
    ContextRecall,
    FactualCorrectness,
    Faithfulness,
)

from ragas_eval.settings import env_or_default, env_required, load_settings, project_path


METRIC_TYPES = {
    "faithfulness": Faithfulness,
    "answer_relevancy": AnswerRelevancy,
    "context_precision": ContextPrecision,
    "context_recall": ContextRecall,
    "factual_correctness": FactualCorrectness,
}


async def score_records(
    records: list[dict[str, Any]],
    metrics: dict[str, Any],
    max_workers: int,
) -> list[dict[str, Any]]:
    semaphore = asyncio.Semaphore(max_workers)

    async def score_one(record: dict[str, Any]) -> dict[str, Any]:
        async with semaphore:
            values: dict[str, Any] = {}
            common = {
                "user_input": record["question"],
                "response": record["response"],
                "retrieved_contexts": record.get("retrieved_contexts", []),
                "reference": record.get("reference", ""),
            }
            arguments = {
                "faithfulness": {
                    key: common[key]
                    for key in ("user_input", "response", "retrieved_contexts")
                },
                "answer_relevancy": {
                    key: common[key] for key in ("user_input", "response")
                },
                "context_precision": {
                    key: common[key]
                    for key in ("user_input", "reference", "retrieved_contexts")
                },
                "context_recall": {
                    key: common[key]
                    for key in ("user_input", "retrieved_contexts", "reference")
                },
                "factual_correctness": {
                    key: common[key] for key in ("response", "reference")
                },
            }
            for name, metric in metrics.items():
                try:
                    result = await metric.ascore(**arguments[name])
                    values[name] = float(result.value)
                    values[f"{name}_reason"] = result.reason
                except Exception as exc:
                    values[name] = math.nan
                    values[f"{name}_reason"] = str(exc)
            return values

    return await asyncio.gather(*(score_one(record) for record in records))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Score collected RAG responses with RAGAS")
    parser.add_argument("--config", help="YAML configuration path")
    parser.add_argument("--input", help="Override collected input path")
    parser.add_argument("--report-dir", help="Override report directory")
    return parser.parse_args()


def require_references(records: list[dict[str, Any]], enabled: list[str]) -> None:
    needs_reference = {"context_precision", "context_recall", "factual_correctness"}
    if not needs_reference.intersection(enabled):
        return
    missing = [
        str(record.get("id", index))
        for index, record in enumerate(records)
        if not str(record.get("reference", "")).strip()
    ]
    if missing:
        raise RuntimeError(
            "reference is required for selected metrics; missing cases: "
            + ", ".join(missing)
        )


def markdown_report(
    frame: pd.DataFrame,
    enabled: list[str],
    thresholds: dict[str, float],
    metadata: dict[str, Any],
) -> str:
    lines = [
        "# RAGAS Evaluation Report",
        "",
        f"- Generated: {datetime.now().isoformat(timespec='seconds')}",
        f"- Samples: {len(frame)}",
        f"- Evaluator model: `{metadata['llm_model']}`",
        f"- Embedding model: `{metadata['embedding_model']}`",
        "",
        "## Summary",
        "",
        "| Metric | Average | Threshold | Result |",
        "|---|---:|---:|---|",
    ]
    for name in enabled:
        average = float(frame[name].dropna().mean()) if name in frame else math.nan
        threshold = float(thresholds.get(name, 0))
        passed = not math.isnan(average) and average >= threshold
        lines.append(
            f"| {name} | {average:.4f} | {threshold:.4f} | "
            f"{'PASS' if passed else 'FAIL'} |"
        )
    lines.extend(["", "## Per case", ""])
    display = ["id", "question", *enabled, "latency_ms"]
    available = [column for column in display if column in frame.columns]
    lines.append(frame[available].to_markdown(index=False))
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    settings = load_settings(args.config)
    paths = settings["paths"]
    evaluator = settings["evaluator"]
    metrics_config = settings["metrics"]
    input_path = project_path(args.input or paths["collected"])
    report_dir = project_path(args.report_dir or paths["report_dir"])
    records = json.loads(input_path.read_text(encoding="utf-8"))
    records = [record for record in records if not record.get("collection_error")]
    if not records:
        raise RuntimeError("no successfully collected records to evaluate")

    enabled = list(metrics_config["enabled"])
    unknown = sorted(set(enabled) - set(METRIC_TYPES))
    if unknown:
        raise RuntimeError(f"unsupported metrics: {', '.join(unknown)}")
    require_references(records, enabled)

    api_key = env_required(evaluator["api_key_env"])
    base_url = env_required(evaluator["base_url_env"])
    llm_model = env_or_default(
        evaluator["llm_model_env"], evaluator["default_llm_model"]
    )
    embedding_model = env_or_default(
        evaluator["embedding_model_env"], evaluator["default_embedding_model"]
    )
    async_client = AsyncOpenAI(
        api_key=api_key,
        base_url=base_url,
        timeout=float(evaluator["timeout_seconds"]),
        max_retries=2,
    )
    judge_llm = llm_factory(llm_model, provider="openai", client=async_client)
    judge_embeddings = OpenAIEmbeddings(client=async_client, model=embedding_model)

    metrics: dict[str, Any] = {}
    for name in enabled:
        if name == "answer_relevancy":
            metrics[name] = AnswerRelevancy(
                llm=judge_llm, embeddings=judge_embeddings
            )
        else:
            metrics[name] = METRIC_TYPES[name](llm=judge_llm)

    scores = pd.DataFrame(
        asyncio.run(
            score_records(records, metrics, int(evaluator["max_workers"]))
        )
    )
    metadata_columns = pd.DataFrame(
        [
            {
                "id": record.get("id", f"case-{index:03d}"),
                "question": record["question"],
                "response": record["response"],
                "tags": ",".join(record.get("tags", [])),
                "latency_ms": record.get("latency_ms"),
                "source_types": ",".join(record.get("source_types", [])),
            }
            for index, record in enumerate(records, start=1)
        ]
    )
    frame = pd.concat([metadata_columns, scores], axis=1)
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    csv_path = report_dir / f"ragas-{stamp}.csv"
    json_path = report_dir / f"ragas-{stamp}.json"
    markdown_path = report_dir / f"ragas-{stamp}.md"
    frame.to_csv(csv_path, index=False)
    frame.to_json(json_path, orient="records", force_ascii=False, indent=2)
    markdown_path.write_text(
        markdown_report(
            frame,
            enabled,
            metrics_config.get("thresholds", {}),
            {"llm_model": llm_model, "embedding_model": embedding_model},
        ),
        encoding="utf-8",
    )
    print(f"csv={csv_path}")
    print(f"json={json_path}")
    print(f"markdown={markdown_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"evaluation failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
