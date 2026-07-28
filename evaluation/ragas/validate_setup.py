#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys

import openai
import pandas
import ragas
import requests
import yaml

from ragas_eval.client import RAGReactorClient
from ragas_eval.settings import load_settings, project_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the local RAGAS setup")
    parser.add_argument("--config")
    parser.add_argument("--check-server", action="store_true")
    args = parser.parse_args()
    settings = load_settings(args.config)
    dataset_path = project_path(settings["paths"]["dataset"])
    json.loads(dataset_path.read_text(encoding="utf-8"))
    print(f"ragas={ragas.__version__}")
    print(f"openai={openai.__version__}")
    print(f"pandas={pandas.__version__}")
    print(f"config={settings['_config_path']}")
    print(f"dataset={dataset_path}")
    if args.check_server:
        server = settings["server"]
        health = RAGReactorClient(
            server["base_url"],
            float(server["connect_timeout_seconds"]),
            float(server["answer_timeout_seconds"]),
        ).health()
        print(
            "server="
            + json.dumps(
                {
                    "status": health.get("status"),
                    "rag_configured": health.get("rag_configured"),
                    "index_ready": health.get("index_ready"),
                },
                ensure_ascii=False,
            )
        )
    print("setup: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"setup validation failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
