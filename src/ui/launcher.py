#!/usr/bin/env python3
import sys
import json
import subprocess

def run_cmd(args):
    cmd = ["shaula"] + args + ["--json"]
    try:
        # Assumes shaula is in PATH or we can run it via ./zig-out/bin/shaula
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
        return json.loads(res.stdout)
    except json.JSONDecodeError:
        return {
            "ok": False,
            "error": {"code": "ERR_UI_INTERNAL_PARSE", "message": "Failed to parse backend output"}
        }
    except FileNotFoundError:
        return {
            "ok": False,
            "error": {"code": "ERR_UI_INTERNAL_SPAWN", "message": "shaula binary not found"}
        }

def show_state(state):
    # Mapping exactly to ERR_* semantics
    if state["ok"]:
        print(f"[OK] {state.get('command')}: {state.get('result')}")
    else:
        err = state.get("error", {})
        code = err.get("code", "ERR_UNKNOWN")
        msg = err.get("message", "Unknown error")
        print(f"[ERROR state] {code} -> {msg}")
        if err.get("retryable"):
            print("  (This error is retryable, degraded state)")
        else:
            print("  (Terminal error state)")

def launcher():
    print("--- Shaula MVP UI Launcher ---")
    print("1. Capture Area")
    print("2. Capture Fullscreen")
    print("3. History Quick List")
    print("4. Check Capabilities (Overlay Controls simulation)")
    choice = input("Select an option: ")

    if choice == "1":
        print("[LOADING] Capturing area...")
        show_state(run_cmd(["capture", "area"]))
    elif choice == "2":
        print("[LOADING] Capturing fullscreen...")
        show_state(run_cmd(["capture", "fullscreen"]))
    elif choice == "3":
        print("[LOADING] Fetching history...")
        show_state(run_cmd(["history", "list"]))
    elif choice == "4":
        print("[LOADING] Checking capabilities...")
        show_state(run_cmd(["capabilities", "list"]))
    else:
        print("Invalid choice")

if __name__ == "__main__":
    launcher()
