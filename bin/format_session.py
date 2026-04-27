#!/usr/bin/env python3
"""
Formats session.jsonl (Codex chat log) into a compact, token-efficient
representation for agent re-ingestion.

Usage:
    python3 format_session.py session.jsonl [--patches-only] [--diffs-only] [--turn TURN_ID]

Modes:
    (default)       Full conversation: user/agent messages, execs, patches
    --patches-only  Only PATCH_INPUT and PATCH OK/FAIL with diffs
    --diffs-only    Only confirmed PATCH OK/FAIL diffs (most compact; for finding a change)
"""

import sys, json, textwrap, re

SKIP_TYPES = {
    "token_count", "context_compacted", "exec_command_startup",
    "exec_command_stdin", "approval_request", "approval_response",
    "turn_context",          # mostly system config noise
}

# response_item subtypes to skip entirely
SKIP_RI_TYPES = {"reasoning", "tool_search_call", "tool_search_output"}

# event_msg developer/system message role
SKIP_MSG_ROLES = {"developer"}

def truncate(s, n=800):
    s = str(s)
    return s if len(s) <= n else s[:n] + f"…[+{len(s)-n}]"

def fmt_args(s):
    """Pretty-print JSON arguments if parseable, otherwise truncate."""
    try:
        d = json.loads(s)
        # For exec commands show cmd prominently
        if "cmd" in d:
            return f'cmd={truncate(d["cmd"], 200)}'
        return truncate(json.dumps(d, separators=(",", ":")), 400)
    except Exception:
        return truncate(s, 400)

def fmt_output(s):
    """Strip token-count headers from exec output, keep the meat."""
    lines = str(s).splitlines()
    # Strip Codex exec wrapper preamble (Chunk ID / Wall time / Process exited / token count)
    clean = [l for l in lines if not re.match(
        r"(Chunk ID:|Wall time:|Process exited|Original token count:)", l)]
    return truncate("\n".join(clean), 1000)

def fmt_patch_input(s):
    """Extract just the diff from apply_patch input."""
    return truncate(s, 2000)

def fmt_unified_diff(d):
    """Print unified diffs from patch_apply_end.changes."""
    out = []
    for path, info in d.items():
        short = path.split("/spi/")[-1]
        out.append(f"  [{info['type']}] {short}")
        diff = info.get("unified_diff","")
        if diff:
            out.append(textwrap.indent(truncate(diff, 1200), "    "))
    return "\n".join(out)

def process(path, patches_only=False, diffs_only=False, filter_turn=None):
    lines = []
    with open(path) as f:
        for raw in f:
            raw = raw.strip()
            if not raw: continue
            lines.append(json.loads(raw))

    current_turn = None

    for obj in lines:
        outer_type = obj.get("type")
        payload = obj.get("payload", {})
        ts = obj.get("timestamp","")[:19]

        if outer_type in SKIP_TYPES:
            continue

        if outer_type == "session_meta":
            if not patches_only and not diffs_only:
                print(f"[SESSION] id={payload.get('id','')} cwd={payload.get('cwd','')} "
                      f"cli={payload.get('cli_version','')} model={payload.get('model_provider','')}\n")
            continue

        if outer_type == "compacted":
            if not patches_only and not diffs_only:
                print(f"[COMPACTED] {truncate(payload.get('message',''))}\n")
            continue

        if outer_type == "event_msg":
            t = payload.get("type","")
            turn_id = payload.get("turn_id", current_turn)

            if filter_turn and turn_id and filter_turn not in turn_id:
                continue

            if t == "task_started":
                current_turn = payload.get("turn_id","")
                if not patches_only and not diffs_only:
                    print(f"\n{'='*70}")
                    print(f"[TURN] {current_turn}")
                    print(f"{'='*70}")
                elif diffs_only:
                    print(f"\n[TURN] {current_turn}")
                continue

            if t == "task_complete":
                if not patches_only and not diffs_only:
                    print(f"\n[TURN COMPLETE] {truncate(payload.get('last_agent_message',''), 400)}\n")
                continue

            if t == "turn_aborted":
                if not patches_only and not diffs_only:
                    print(f"\n[TURN ABORTED] reason={payload.get('reason')}\n")
                continue

            if t == "user_message":
                if not patches_only and not diffs_only:
                    print(f"\n[USER]\n{truncate(payload.get('message',''), 1200)}\n")
                continue

            if t == "agent_message":
                if not patches_only and not diffs_only:
                    phase = payload.get("phase","")
                    print(f"[AGENT:{phase}] {truncate(payload.get('message',''), 600)}\n")
                continue

            if t == "exec_command_end":
                if not patches_only and not diffs_only:
                    cmd = payload.get("command", [])
                    cmd_str = cmd[-1] if cmd else ""
                    out = fmt_output(payload.get("aggregated_output",""))
                    print(f"[EXEC] {truncate(cmd_str, 200)}")
                    if out.strip():
                        print(textwrap.indent(out, "  "))
                    print()
                continue

            if t == "patch_apply_end":
                success = payload.get("success", False)
                changes = payload.get("changes", {})
                print(f"[PATCH {'OK' if success else 'FAIL'}]")
                print(fmt_unified_diff(changes))
                print()
                continue

            if t == "mcp_tool_call_end":
                if not patches_only and not diffs_only:
                    inv = payload.get("invocation", {})
                    result_ok = payload.get("result", {}).get("Ok")
                    result_text = ""
                    if result_ok:
                        for c in result_ok.get("content", []):
                            result_text += c.get("text","")
                    print(f"[MCP] {inv.get('server','')}.{inv.get('tool','')} "
                          f"args={json.dumps(inv.get('arguments',''), separators=(',',':'))}")
                    if result_text.strip():
                        print(textwrap.indent(truncate(result_text, 600), "  "))
                    print()
                continue

            if t == "error":
                print(f"[ERROR] {payload.get('message','')}\n")
                continue

            # fallthrough: skip other event_msg types silently
            continue

        if outer_type == "response_item":
            if filter_turn:
                continue  # response_items don't carry turn_id, skip when filtering

            ri_type = payload.get("type","")

            if ri_type in SKIP_RI_TYPES:
                continue

            if ri_type == "message":
                if payload.get("role") in SKIP_MSG_ROLES:
                    continue
                if not patches_only and not diffs_only:
                    for c in (payload.get("content") or []):
                        if c.get("type") == "output_text":
                            print(f"[MSG:{payload.get('role','?')}] {truncate(c.get('text',''), 600)}\n")
                continue

            if ri_type == "function_call":
                if not patches_only and not diffs_only:
                    print(f"[CALL] {payload.get('name','')} {fmt_args(payload.get('arguments',''))}\n")
                continue

            if ri_type == "function_call_output":
                if not patches_only and not diffs_only:
                    print(f"[CALL_OUT] {fmt_output(payload.get('output',''))}\n")
                continue

            if ri_type == "custom_tool_call":
                name = payload.get("name","")
                inp = payload.get("input","")
                if name == "apply_patch":
                    if not diffs_only:
                        print(f"[PATCH_INPUT]\n{fmt_patch_input(inp)}\n")
                elif not patches_only and not diffs_only:
                    print(f"[CUSTOM:{name}] {truncate(str(inp), 400)}\n")
                continue

            if ri_type == "custom_tool_call_output":
                if not patches_only and not diffs_only:
                    print(f"[CUSTOM_OUT] {truncate(payload.get('output',''), 400)}\n")
                continue

if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("file", help="Path to session.jsonl")
    p.add_argument("--diffs-only", action="store_true",
                   help="Only show confirmed patch diffs (PATCH OK/FAIL) — most compact")
    p.add_argument("--patches-only", action="store_true",
                   help="Show patch inputs and confirmed diffs (no execs/messages)")
    p.add_argument("--turn", default=None,
                   help="Filter to a specific turn_id substring")
    args = p.parse_args()
    process(args.file, patches_only=args.patches_only, diffs_only=args.diffs_only, filter_turn=args.turn)
