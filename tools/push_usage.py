#!/usr/bin/env python3
"""Collect Claude / Codex / Antigravity usage from the local CLIs and push it to the SmallTV.

One run = one collection + one push (launchd runs it every 60 s).

    SMALLTV_HOST=192.168.1.50 SMALLTV_TOKEN=... python3 tools/push_usage.py   # real data -> device
    python3 tools/push_usage.py --print                     # collect and print the payload, don't push

Install it as a launchd agent (every 60 s) with tools/install_launchd.sh.

Login tokens belong to the CLIs: they are only read here, never printed or written.
Standard library only, so it runs with the system python3.
"""
import argparse
import fcntl
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECRETS = ROOT / "firmware" / "usagebar" / "include" / "secrets.h"


def log(msg):
    print(f"{datetime.now():%H:%M:%S} {msg}", file=sys.stderr)


def short(text, n=31):
    text = " ".join(str(text).split())
    return text if len(text) <= n else text[: n - 1] + "~"


def iso_to_unix(value):
    if not value:
        return 0
    if isinstance(value, (int, float)):
        return int(value / 1000 if value > 1e12 else value)
    try:
        return int(datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())
    except ValueError:
        return 0


def http_json(url, headers, timeout=15):
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def window(label, used, reset):
    return {"l": label, "u": None if used is None else round(float(used)), "r": iso_to_unix(reset)}


def label_for_seconds(secs, fallback):
    return {18000: "5h", 604800: "7d", 86400: "1d"}.get(int(secs or 0), fallback)


def provider(pid, name, plan="", err="", windows=()):
    return {"id": pid, "n": name, "plan": short(plan, 13), "err": short(err), "w": list(windows)[:2], "at": 0}


# ---------- fetch scheduling ----------

# Each run pushes every minute, but each service is only queried this often (the usage APIs
# rate-limit: Claude answered 429 when polled every 60 s). In between, the last good result is resent.
FETCH_INTERVAL_S = {"claude": 300, "codex": 300, "antigravity": 300}
ERROR_BACKOFF_S = 600
NAMES = {"claude": "Claude", "codex": "Codex", "antigravity": "Antigravity"}
CACHE = Path.home() / "Library" / "Caches" / "smalltv-usage.json"


def retry_after(http_error):
    try:
        return int(http_error.headers.get("Retry-After", 0))
    except (TypeError, ValueError):
        return 0


def load_cache():
    try:
        return json.loads(CACHE.read_text())
    except (OSError, ValueError):
        return {}


def cached(pid, fetch, cache, now):
    """Query a service at most once per interval (longer after an error); otherwise reuse the cache.

    Results carry "at" (unix time of the last successful fetch) so the device can show how old the
    numbers are. While a service is failing, the last good numbers are sent with the failure reason.
    """
    entry = cache.get(pid) or {}
    if now < entry.get("next", 0):
        # Not due yet (or backing off): never call the service, even when there is no good data yet.
        return entry.get("shown") or entry.get("data") or provider(pid, NAMES.get(pid, pid), err="waiting")
    result = fetch()
    retry = result.pop("_retry", 0)
    if not result["err"]:
        result["at"] = now
        cache[pid] = {"data": result, "next": now + FETCH_INTERVAL_S.get(pid, 300)}
        return result
    reason = "limited" if "429" in result["err"] else result["err"]
    backoff = max(retry, ERROR_BACKOFF_S)
    log(f"{pid}: {reason}, next try in {backoff} s")
    good = entry.get("data")
    shown = {**good, "err": reason} if good else {**result, "err": reason, "at": 0}
    cache[pid] = {"data": good, "shown": shown, "next": now + backoff}
    return shown


def find_cli(name):
    return shutil.which(name) or next(
        (str(p) for p in (Path.home() / ".local/bin" / name, Path("/usr/local/bin") / name) if p.exists()), None
    )


# CLIs that lose their login try to open a browser sign-in page. Unattended (every few minutes,
# overnight) that piled up Google sign-in windows, so the CLIs run with `open` replaced by a no-op.
SHIM_DIR = Path.home() / "Library" / "Caches" / "smalltv-usage-shim"
BLOCKED_LOG = Path.home() / "Library" / "Logs" / "smalltv-usage-blocked-open.log"


def cli_env():
    shim = SHIM_DIR / "open"
    if not shim.exists():
        SHIM_DIR.mkdir(parents=True, exist_ok=True)
        shim.write_text(f'#!/bin/sh\necho "$(date "+%F %T") blocked a browser sign-in window" >> "{BLOCKED_LOG}"\nexit 0\n')
        shim.chmod(0o755)
    env = dict(os.environ)
    env["PATH"] = f"{SHIM_DIR}:{env.get('PATH', '')}"
    env["BROWSER"] = str(shim)
    return env


def run_cli(name, *args, timeout=60):
    """Run a CLI headless in an empty directory. Returns (returncode, stdout, stderr).

    It runs in its own process group so a timeout also kills anything it spawned (e.g. MCP servers).
    """
    exe = find_cli(name)
    if not exe:
        return None, "", f"{name} not installed"
    with tempfile.TemporaryDirectory() as tmp:
        p = subprocess.Popen([exe, *args], cwd=tmp, env=cli_env(), stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            out, err = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            raise
    return p.returncode, out, err


def classify(text):
    """Short on-screen reason from CLI/HTTP error text (no URLs or secrets)."""
    t = text.lower()
    if any(k in t for k in ("login", "log in", "sign in", "signin", "auth", "credential", "unauthorized", "401")):
        return "login"
    if any(k in t for k in ("network", "connect", "timed out", "timeout", "resolve", "dns", "unreachable", "offline")):
        return "offline"
    return ""


def first_line(text, n=120):
    line = next((l.strip() for l in text.splitlines() if l.strip()), "")
    return re.sub(r"https?://\S+", "<url>", line)[:n]


# ---------- Claude (`claude -p /usage`: local command, no model call; the CLI refreshes its own token) ----------

CLAUDE_LINE = re.compile(r"Current (session|week \(all models\)):\s*(\d+)% used\s*·\s*resets (.+?) \(([^)]+)\)")


def claude_reset(text, tz):
    """'Oct 6 at 2:10pm' / 'Oct 8 at 12am' in `tz` -> unix seconds."""
    from zoneinfo import ZoneInfo

    now = datetime.now(ZoneInfo(tz))
    for fmt in ("%b %d at %I:%M%p", "%b %d at %I%p"):
        try:  # parse with the year included, otherwise "Feb 29" fails (strptime defaults to 1900)
            dt = datetime.strptime(f"{now.year} {text.strip()}", "%Y " + fmt).replace(tzinfo=ZoneInfo(tz))
        except ValueError:
            continue
        if (now - dt).days > 180:  # "Jan 2" seen in late December
            dt = dt.replace(year=now.year + 1)
        return int(dt.timestamp())
    return 0


def claude_plan():
    """Plan name only (e.g. 'Max') from the CLI's keychain item; the token itself is not used."""
    try:
        raw = subprocess.run(["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
                             capture_output=True, text=True, timeout=10)
        return ((json.loads(raw.stdout).get("claudeAiOauth") or {}).get("subscriptionType") or "").capitalize()
    except Exception:  # noqa: BLE001
        return ""


def claude():
    try:
        rc, out, err = run_cli("claude", "-p", "/usage", "--output-format", "json")
        if rc is None:
            return provider("claude", "Claude", err="claude not installed")
        data = json.loads(out[out.find("{"):]) if "{" in out else {}
        text = str(data.get("result") or "")
        wins = []
        for kind, used, reset, tz in CLAUDE_LINE.findall(text):
            wins.append({"l": "5h" if kind == "session" else "7d", "u": int(used), "r": claude_reset(reset, tz)})
        if wins:
            return provider("claude", "Claude", claude_plan(), windows=wins)
        reason = classify(text + " " + err) or "no usage data"
        log(f"claude: exit {rc}: {first_line(text or err)}")
        return provider("claude", "Claude", err=reason)
    except subprocess.TimeoutExpired:
        return provider("claude", "Claude", err="claude timeout")
    except Exception as e:  # noqa: BLE001 - one provider failing must not stop the others
        return provider("claude", "Claude", err=type(e).__name__)


# ---------- Codex (token from ~/.codex/auth.json written by the `codex` CLI) ----------

def codex_tokens():
    home = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
    return json.loads((home / "auth.json").read_text()).get("tokens") or {}


def codex_fetch(tokens):
    headers = {"Authorization": f"Bearer {tokens['access_token']}", "User-Agent": "codex-cli"}
    if tokens.get("account_id"):
        headers["ChatGPT-Account-Id"] = tokens["account_id"]
    return http_json("https://chatgpt.com/backend-api/wham/usage", headers)


def codex():
    try:
        tokens = codex_tokens()
        if not tokens.get("access_token"):
            return provider("codex", "Codex", err="login")
        data = codex_fetch(tokens)
        rl = data.get("rate_limit") or {}
        wins = []
        for key, fallback in (("primary_window", "5h"), ("secondary_window", "7d")):
            w = rl.get(key)
            if w:
                label = label_for_seconds(w.get("limit_window_seconds"), fallback)
                wins.append(window(label, w.get("used_percent"), w.get("reset_at")))
        return provider("codex", "Codex", (data.get("plan_type") or "").capitalize(), windows=wins)
    except FileNotFoundError:
        return provider("codex", "Codex", err="login")
    except urllib.error.HTTPError as e:
        err = "login" if e.code == 401 else f"HTTP {e.code}"
        return {**provider("codex", "Codex", err=err), "_retry": retry_after(e)}
    except urllib.error.URLError as e:
        log(f"codex: network error: {first_line(str(e.reason))}")
        return provider("codex", "Codex", err="offline")
    except Exception as e:  # noqa: BLE001
        return provider("codex", "Codex", err=type(e).__name__)


# ---------- Antigravity (`agy -p /usage --output-format json`, no model call) ----------

AGY_GROUPS = ("gemini",)  # quota groups to show; "Claude and GPT models" is skipped


def agy_windows(data):
    """command.data.groups[].buckets[] -> windows for the Gemini group, in the CLI's order."""
    wins = []
    for group in ((data.get("command") or {}).get("data") or {}).get("groups") or []:
        if not any(g in (group.get("name") or "").lower() for g in AGY_GROUPS):
            continue
        for bucket in group.get("buckets") or []:
            frac = bucket.get("remaining_fraction")
            if isinstance(frac, (int, float)):
                label = {"weekly": "7d", "daily": "1d"}.get(bucket.get("window"), "Gem")
                wins.append(window(label, (1 - frac) * 100, bucket.get("reset_time")))
    return wins


def antigravity():
    try:
        rc, out, err = run_cli("agy", "-p", "/usage", "--output-format", "json", timeout=90)
        if rc is None:
            return provider("antigravity", "Antigravity", err="agy not installed")
        if rc != 0:
            log(f"antigravity: exit {rc}: {first_line(err or out)}")
            return provider("antigravity", "Antigravity", err=classify(err + " " + out) or f"agy exit {rc}")
        if "{" not in out:
            return provider("antigravity", "Antigravity", err="no quota data")
        wins = agy_windows(json.loads(out[out.find("{"):]))
        if not wins:
            return provider("antigravity", "Antigravity", err="no quota data")
        return provider("antigravity", "Antigravity", windows=wins)
    except subprocess.TimeoutExpired:
        return provider("antigravity", "Antigravity", err="agy timeout")
    except Exception as e:  # noqa: BLE001
        return provider("antigravity", "Antigravity", err=type(e).__name__)


# ---------- mock / push ----------

# ---------- weather (Open-Meteo, no API key) ----------

WEATHER_URL = ("https://api.open-meteo.com/v1/forecast?latitude={lat:.4f}&longitude={lon:.4f}"
               "&current=weather_code,temperature_2m,is_day&timezone=auto")
WEATHER_INTERVAL_S = 900
DEFAULT_PLACE = {"city": "Seoul", "lat": 37.5665, "lon": 126.9780}


def device_place(host, cache):
    """The weather place chosen on the device's web page (falls back to the last known one)."""
    try:
        s = http_json(f"http://{host}/api/settings", {}, timeout=5)
        place = {"city": s.get("city", ""), "lat": float(s["lat"]), "lon": float(s["lon"])}
        cache["_place"] = place
        return place
    except Exception:  # noqa: BLE001 - older firmware or device busy
        return cache.get("_place") or DEFAULT_PLACE


def weather_icon(code, is_day):
    """WMO weather code -> icon name in tools/icons/weather.json."""
    if code in (0, 1):
        return "clear_day" if is_day else "clear_night"
    if code == 2:
        return "partly_cloudy"
    if code == 3:
        return "cloudy"
    if code in (45, 48):
        return "fog"
    if code in (71, 73, 75, 77, 85, 86):
        return "snow"
    if code >= 95:
        return "thunder"
    return "rain"  # drizzle, rain, freezing rain, showers


def weather(cache, now, place):
    """Current weather + the place's UTC offset (the device's clock follows it)."""
    entry = cache.get("_wx") or {}
    key = f"{place['lat']:.3f},{place['lon']:.3f}"
    if now >= entry.get("next", 0) or entry.get("key") != key:  # a new place is fetched right away
        try:
            r = http_json(WEATHER_URL.format(**place), {"User-Agent": "smalltv-usage/1.0"})
            cur = r["current"]
            entry = {"data": {"icon": weather_icon(cur["weather_code"], cur.get("is_day", 1)),
                              "t": round(float(cur["temperature_2m"]), 1)},
                     "tz_off": int(r.get("utc_offset_seconds", 0)), "key": key,
                     "next": now + WEATHER_INTERVAL_S}
            if entry["key"] != (cache.get("_wx") or {}).get("key"):
                log(f"weather place: {place.get('city') or key}")
        except Exception as e:  # noqa: BLE001 - weather is optional
            log(f"weather: {type(e).__name__}")
            entry["next"] = now + ERROR_BACKOFF_S
        cache["_wx"] = entry
    return entry.get("data")


def internet_ok():
    """True when Apple's captive-portal probe answers normally and HTTPS gets out.

    This Mac's network login expires every 8 hours; until someone logs in again nothing outside
    the hotspot is reachable, so the services are not queried (and the CLIs are not run) meanwhile.
    The office portal lets the Apple probe through and only breaks HTTPS (its own certificate), so
    two hosts the collector needs anyway are tried too: any HTTP answer counts, TLS/DNS errors don't.
    """
    try:
        with urllib.request.urlopen("http://captive.apple.com/hotspot-detect.html", timeout=5) as r:
            if b"Success" not in r.read(4096):
                return False
    except Exception:  # noqa: BLE001 - any failure means "not online"
        return False
    for url in ("https://chatgpt.com", "https://api.open-meteo.com"):
        try:
            urllib.request.urlopen(urllib.request.Request(url, method="HEAD"), timeout=5).close()
            return True
        except urllib.error.HTTPError:
            return True
        except Exception:  # noqa: BLE001
            pass
    return False


OFFLINE_AFTER_FAILS = 2  # one slow captive-portal probe is not an outage


def collect(cache, now, host):
    """Returns (net_state, net_since, providers, weather, tz_off) and updates the cache in place."""
    net = cache.get("_net") or {"state": "ok", "since": 0, "fails": 0}
    cached_view = lambda: [(cache.get(pid) or {}).get("shown") or (cache.get(pid) or {}).get("data")  # noqa: E731
                           or provider(pid, name, err="offline") for pid, name in NAMES.items()]
    if internet_ok():
        if net["state"] != "ok":
            log("internet is back")
            if now - net["since"] > max(FETCH_INTERVAL_S.values()):  # numbers are old: refresh all now
                for pid in NAMES:
                    if pid in cache:
                        cache[pid]["next"] = 0
        net = {"state": "ok", "since": 0, "fails": 0}
        providers = [cached("claude", claude, cache, now), cached("codex", codex, cache, now),
                     cached("antigravity", antigravity, cache, now)]
    else:
        fails = net.get("fails", 0) + 1
        if net["state"] == "ok" and fails >= OFFLINE_AFTER_FAILS:
            log("internet offline: network login needed, pausing service queries")
            net = {"state": "login", "since": net.get("first_fail", now), "fails": fails}
        else:
            net = {**net, "fails": fails, "first_fail": net.get("first_fail", now)}
        providers = cached_view()  # never query services while the probe fails
    cache["_net"] = net
    if net["state"] == "ok" and not net.get("fails"):
        weather(cache, now, device_place(host, cache))
    wx_entry = cache.get("_wx") or {}
    return net["state"], net["since"], providers, wx_entry.get("data"), wx_entry.get("tz_off")


def save_cache(cache):
    """Atomic write: a run killed mid-write must not wipe the backoff state."""
    tmp = CACHE.with_suffix(".tmp")
    tmp.write_text(json.dumps(cache))
    os.replace(tmp, CACHE)


def push_token():
    """SMALLTV_TOKEN (shown on the device's web page), else PUSH_TOKEN from your own build's secrets.h."""
    if os.environ.get("SMALLTV_TOKEN"):
        return os.environ["SMALLTV_TOKEN"]
    m = SECRETS.exists() and re.search(r'#define PUSH_TOKEN "([^"]+)"', SECRETS.read_text())
    if not m:
        sys.exit("set SMALLTV_TOKEN to the push token shown on the device's web page")
    return m.group(1)


def push(host, payload):
    body = json.dumps(payload, separators=(",", ":")).encode()
    req = urllib.request.Request(
        f"http://{host}/api/usage", data=body, method="POST",
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {push_token()}"},
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.status, r.read().decode()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("SMALLTV_HOST"), help="display IP (or set SMALLTV_HOST)")
    ap.add_argument("--print", action="store_true", help="print payload instead of pushing")
    args = ap.parse_args()
    if not args.host and not args.print:
        ap.error("set the display's address with --host or SMALLTV_HOST")

    lock = open(CACHE.with_suffix(".lock"), "w")
    try:  # a manual run and the launchd run must not interleave
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        log("another run is in progress, skipping")
        return
    cache = load_cache()
    net, net_since, providers, wx, tz_off = collect(cache, int(time.time()), args.host)
    if not args.print:
        save_cache(cache)
    payload = {"ts": int(time.time()), "net": net, "net_since": net_since, "p": providers}
    if wx:
        payload["wx"] = wx
    if tz_off is not None:
        payload["tz_off"] = tz_off
    if args.print:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
        return
    summary = ", ".join(f"{p['n']}:{p['err'] or '/'.join(str(w['u']) for w in p['w'])}" for p in providers)
    if net != "ok":
        summary = f"NET:{net} " + summary
    if wx:
        summary += f", wx:{wx['icon']} {wx['t']}C"
    try:
        status, text = push(args.host, payload)
        log(f"pushed {status} [{summary}]")
    except Exception as e:  # noqa: BLE001
        log(f"push failed: {e} [{summary}]")
        sys.exit(1)


if __name__ == "__main__":
    main()
