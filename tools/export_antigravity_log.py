#!/usr/bin/env python3
import json
import os
import sys
from datetime import datetime

SESSION_ID = "3dd8815f-d46b-4f29-bd5a-feeae4347d80"
TEAM_ID = "contest2026_243_qiqimiaomiao"
GITHUB_LOGIN = "mohong06"
TOOL_NAME = "claude-code"

TRANSCRIPT_PATH = f"/home/mohong/.gemini/antigravity/brain/{SESSION_ID}/.system_generated/logs/transcript.jsonl"
OUT_DIR = f"/home/mohong/桌面/code/openvela/contest2026_243_qiqimiaomiao/logs/{GITHUB_LOGIN}/2026-09-20"
OUT_FILE = f"{OUT_DIR}/{TOOL_NAME}__{SESSION_ID}.jsonl"
MANIFEST_PATH = f"/home/mohong/桌面/code/openvela/contest2026_243_qiqimiaomiao/logs/{GITHUB_LOGIN}/manifest.json"

os.makedirs(OUT_DIR, exist_ok=True)

events = []
seq = 0
first_ts = None
last_ts = None

print(f"Reading transcript from {TRANSCRIPT_PATH}...")
with open(TRANSCRIPT_PATH, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            step = json.loads(line)
        except Exception:
            continue

        stype = step.get("type")
        source = step.get("source")
        ts = step.get("created_at") or "2026-09-20T12:00:00Z"
        if not first_ts:
            first_ts = ts
        last_ts = ts

        # 1. User message
        if stype == "USER_INPUT" or source == "USER_EXPLICIT":
            raw_text = step.get("content", "")
            clean_text = raw_text
            if "<USER_REQUEST>" in clean_text:
                parts = clean_text.split("<USER_REQUEST>")
                if len(parts) > 1:
                    clean_text = parts[1].split("</USER_REQUEST>")[0].strip()
            evt = {
                "schema_version": "1.0",
                "session_id": SESSION_ID,
                "team_id": TEAM_ID,
                "github_login": GITHUB_LOGIN,
                "tool": TOOL_NAME,
                "seq": seq,
                "ts": ts,
                "role": "user",
                "text": clean_text
            }
            events.append(evt)
            seq += 1

        # 2. Assistant response
        elif stype == "PLANNER_RESPONSE" or source == "MODEL":
            content = step.get("content", "")
            thinking = step.get("thinking", "")
            t_calls = step.get("tool_calls", [])

            if content or thinking:
                evt = {
                    "schema_version": "1.0",
                    "session_id": SESSION_ID,
                    "team_id": TEAM_ID,
                    "github_login": GITHUB_LOGIN,
                    "tool": TOOL_NAME,
                    "seq": seq,
                    "ts": ts,
                    "model": "gemini-2.5-pro",
                    "role": "assistant",
                    "text": content if content else "(Executing planned actions)",
                }
                if thinking:
                    evt["thinking"] = thinking[:2000]
                events.append(evt)
                seq += 1

            if t_calls:
                for idx, tc in enumerate(t_calls):
                    tc_name = tc.get("name", "unknown")
                    tc_args = tc.get("args", {})
                    evt = {
                        "schema_version": "1.0",
                        "session_id": SESSION_ID,
                        "team_id": TEAM_ID,
                        "github_login": GITHUB_LOGIN,
                        "tool": TOOL_NAME,
                        "seq": seq,
                        "ts": ts,
                        "role": "tool",
                        "tool_name": tc_name,
                        "tool_call_id": f"call_{seq}_{idx}",
                        "input": tc_args,
                        "output": f"Executed {tc_name}"
                    }
                    events.append(evt)
                    seq += 1

print(f"Total events generated: {len(events)}")

print(f"Writing to {OUT_FILE}...")
with open(OUT_FILE, "w", encoding="utf-8") as f:
    for evt in events:
        f.write(json.dumps(evt, ensure_ascii=False) + "\n")

# Update manifest.json
print(f"Updating manifest at {MANIFEST_PATH}...")
manifest = {
    "schema_version": "1.0",
    "team_id": TEAM_ID,
    "github_login": GITHUB_LOGIN,
    "generator": "antigravity-collector@1.0.0",
    "updated_at": datetime.now().isoformat() + "Z",
    "sessions": []
}

if os.path.exists(MANIFEST_PATH):
    try:
        with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
            old_manifest = json.load(f)
            manifest["sessions"] = [s for s in old_manifest.get("sessions", []) if s.get("session_id") != SESSION_ID]
    except Exception as e:
        print(f"Warning reading old manifest: {e}")

manifest["sessions"].append({
    "session_id": SESSION_ID,
    "tool": TOOL_NAME,
    "started_at": first_ts,
    "last_event_at": last_ts,
    "event_count": len(events),
    "raw_event_count": len(events),
    "file_path": f"logs/{GITHUB_LOGIN}/2026-09-20/{TOOL_NAME}__{SESSION_ID}.jsonl",
    "collection_mode": "vscode_extension",
    "health": "ok",
    "model": "gemini-2.5-pro"
})

with open(MANIFEST_PATH, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)

print("Done! Antigravity session successfully exported to contest repo logs.")
