from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any

import requests


@dataclass
class CollectedAnswer:
    answer: str
    contexts: list[str]
    context_ids: list[str]
    source_types: list[str]
    grounded: bool
    latency_ms: float
    request_id: str


class RAGReactorClient:
    def __init__(
        self,
        base_url: str,
        connect_timeout: float = 5,
        answer_timeout: float = 60,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = (connect_timeout, answer_timeout)
        self.session = requests.Session()
        # The application server is local. Environment proxies (for example an
        # NPV listener on 127.0.0.1:7897) must not intercept localhost traffic.
        self.session.trust_env = False

    def login(self, username: str, password: str) -> None:
        response = self.session.post(
            f"{self.base_url}/2CGISQL.cgi",
            data={"user": username, "password": password},
            timeout=self.timeout,
        )
        response.raise_for_status()
        if not self.session.cookies.get("sid"):
            raise RuntimeError("login failed: server did not return a sid cookie")
        if not self.session.cookies.get("csrf_token"):
            raise RuntimeError("login failed: server did not return a csrf_token cookie")

    def health(self) -> dict[str, Any]:
        response = self.session.get(f"{self.base_url}/api/health", timeout=self.timeout)
        response.raise_for_status()
        return response.json()

    def ask(
        self,
        question: str,
        top_k: int,
        include_community: bool,
    ) -> CollectedAnswer:
        started = time.perf_counter()
        for attempt in range(8):
            response = self.session.post(
                f"{self.base_url}/api/ask",
                headers={
                    "Content-Type": "application/json",
                    "Accept": "application/json",
                    "X-CSRF-Token": self.session.cookies["csrf_token"],
                },
                json={
                    "question": question,
                    "top_k": top_k,
                    "stream": False,
                    "include_community": include_community,
                },
                timeout=self.timeout,
            )
            if response.status_code != 429:
                break
            time.sleep(2.1)
        latency_ms = (time.perf_counter() - started) * 1000
        if not response.ok:
            try:
                detail = response.json().get("error", {}).get("message", response.text)
            except ValueError:
                detail = response.text
            raise RuntimeError(f"/api/ask returned HTTP {response.status_code}: {detail}")
        payload = response.json()
        sources = payload.get("sources", [])
        return CollectedAnswer(
            answer=payload.get("answer", ""),
            contexts=[item.get("text", "") for item in sources if item.get("text")],
            context_ids=[
                f"{item.get('source_type', 'unknown')}:{item.get('source_id') or item.get('file', '')}"
                for item in sources
            ],
            source_types=[item.get("source_type", "unknown") for item in sources],
            grounded=bool(payload.get("grounded")),
            latency_ms=latency_ms,
            request_id=payload.get("request_id", ""),
        )
