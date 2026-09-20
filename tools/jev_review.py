"""Optional advisory review of explicitly selected text. Never executes answers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from urllib import error, request

ENDPOINT = "https://api.typesafe.ai/v1/systemone"


def encode(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def prepare(state_path, questions_path, model):
    text = Path(state_path).read_text(encoding="utf-8-sig")
    state = json.loads(text) if Path(state_path).suffix == ".json" else text
    questions = json.loads(Path(questions_path).read_text(encoding="utf-8-sig"))
    if not isinstance(questions, dict) or not 1 <= len(questions) <= 32:
        raise ValueError("Supply 1 to 32 independent questions")
    for question in questions.values():
        if not isinstance(question, dict) or not question.get("instructions"):
            raise ValueError("Each question needs instructions")
        kind, criteria = question.get("type"), question.get("criteria")
        if kind not in ("choice", "score", "noul"):
            raise ValueError("Unsupported question type")
        if kind == "choice" and (not isinstance(criteria, dict) or not 1 <= len(criteria) <= 255):
            raise ValueError("Choice needs a criteria map")
        if kind == "score" and (not isinstance(criteria, list) or not 2 <= len(criteria) <= 10):
            raise ValueError("Score needs 2 to 10 levels")
    payload = encode(dict(state=state, model=model, questions=questions))
    if len(payload) > 262144:
        raise ValueError("Review packet exceeds local 256 KiB limit")
    return payload, hashlib.sha256(payload).hexdigest(), questions


def validate(response, questions, model):
    if not isinstance(response, dict) or not isinstance(response.get("model"), str):
        raise ValueError("Response missing model")
    if model != "jev-latest" and response["model"] != model:
        raise ValueError("Response model differs from requested pin")
    answers = response.get("answers")
    if not isinstance(answers, dict) or set(answers) != set(questions):
        raise ValueError("Response question coverage differs")
    for name, question in questions.items():
        answer = answers[name]
        kind = question["type"]
        if not isinstance(answer, dict) or answer.get("type") != kind:
            raise ValueError("Response answer type differs")
        if kind == "choice":
            if answer.get("choice") not in question["criteria"]:
                raise ValueError("Response selected an unknown choice")
        else:
            value = answer.get(kind)
            maximum = 1 if kind == "noul" else len(question["criteria"]) - 1
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= maximum:
                raise ValueError("Response value outside rubric")
    return response


def evaluate(payload, key, timeout):
    call = request.Request(ENDPOINT, data=payload, headers={
        "Authorization": "Bearer " + key, "Content-Type": "application/json"})
    # No automatic retries: a timeout may already have incurred a charge.
    try:
        with request.urlopen(call, timeout=timeout) as stream:
            return json.loads(stream.read(1048577).decode("utf-8"))
    except error.HTTPError as exc:
        raise RuntimeError("Jev HTTP {}; review unresolved".format(exc.code)) from None
    except (error.URLError, TimeoutError, OSError):
        raise RuntimeError("Jev connection failed or timed out; review unresolved") from None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", required=True)
    parser.add_argument("--questions", required=True)
    parser.add_argument("--model", default="jev-1.13.0")
    parser.add_argument("--cache-dir", default="run-source/jev-cache-python")
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-cache", action="store_true")
    args = parser.parse_args(argv)
    started = time.monotonic()
    if not 0 < args.timeout <= 60:
        raise ValueError("Timeout must be in (0, 60] seconds")
    payload, digest, questions = prepare(args.state, args.questions, args.model)
    cache = Path(args.cache_dir) / (digest + ".json")
    # Mutable aliases are never read from or written to persistent cache.
    cacheable = not args.no_cache and args.model != "jev-latest"
    if args.dry_run:
        print(json.dumps(dict(endpoint=ENDPOINT, request_hash=digest,
                              request_bytes=len(payload), questions=list(questions),
                              model=args.model, cacheable=cacheable), indent=2))
        return 0
    cached = cacheable and cache.is_file()
    if cached:
        response = json.loads(cache.read_text(encoding="utf-8"))
    else:
        key = os.environ.get("TYPESAFE_API_KEY")
        if not key:
            raise RuntimeError("TYPESAFE_API_KEY is not set; local work can continue")
        response = evaluate(payload, key, args.timeout)
    validate(response, questions, args.model)
    if cacheable and not cached:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(encode(response))
    print(json.dumps(dict(advisory_only=True, request_hash=digest, cached=cached,
                          elapsed_seconds=time.monotonic() - started,
                          response=response), indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError) as exc:
        print("Review unresolved: " + str(exc), file=sys.stderr)
        sys.exit(2)
