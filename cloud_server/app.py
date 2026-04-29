import os
import json
import time
import random
import requests
from flask import Flask, render_template, request, jsonify, redirect, url_for
from dotenv import load_dotenv
import redis

load_dotenv()

app = Flask(__name__)

# ---------------------------------------------------------------------------
# Konfiguracja
# ---------------------------------------------------------------------------

SESSION_TTL_SECONDS = int(os.environ.get("SESSION_TTL_SECONDS", 300))
NVIDIA_API_KEY = os.environ.get("NVIDIA_API_KEY", "your_key_here")
REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379")

# ---------------------------------------------------------------------------
# Redis — lazy connection (nie łączy przy imporcie, tylko przy pierwszym użyciu)
# ---------------------------------------------------------------------------

_redis_client = None

def get_redis():
    global _redis_client
    if _redis_client is None:
        _redis_client = redis.from_url(REDIS_URL, decode_responses=True)
    return _redis_client

# ---------------------------------------------------------------------------
# Pomocnicze funkcje
# ---------------------------------------------------------------------------

_CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def generate_code() -> str:
    part1 = "".join(random.choices(_CODE_CHARS, k=3))
    part2 = "".join(random.choices(_CODE_CHARS, k=3))
    return f"{part1}-{part2}"


def session_key(code: str) -> str:
    return f"robot:session:{code}"


def queue_key(code: str) -> str:
    return f"robot:queue:{code}"


def get_session(code: str):
    try:
        data = get_redis().hgetall(session_key(code))
        return data if data else None
    except Exception as e:
        print(f"[REDIS ERROR] get_session: {e}")
        return None


def refresh_ttl(code: str):
    try:
        r = get_redis()
        r.expire(session_key(code), SESSION_TTL_SECONDS)
        r.expire(queue_key(code), SESSION_TTL_SECONDS)
    except Exception as e:
        print(f"[REDIS ERROR] refresh_ttl: {e}")


def push_event(code: str, event: dict):
    try:
        r = get_redis()
        r.rpush(queue_key(code), json.dumps(event))
        r.expire(queue_key(code), SESSION_TTL_SECONDS)
    except Exception as e:
        print(f"[REDIS ERROR] push_event: {e}")


CMD_MAP = {
    "F": "przód",
    "B": "tył",
    "L": "lewo",
    "R": "prawo",
    "S": "stop",
    "W": "wyprostuj",
}

# ---------------------------------------------------------------------------
# Strony HTML
# ---------------------------------------------------------------------------


@app.route("/")
def home():
    return render_template("home.html")


@app.route("/join", methods=["GET", "POST"])
def join():
    if request.method == "POST":
        code = request.form.get("code", "").strip().upper().replace(" ", "")
    else:
        code = request.args.get("code", "").strip().upper().replace(" ", "")

    if not code:
        return redirect(url_for("home"))

    if not get_session(code):
        return render_template(
            "home.html",
            error=f"Kod '{code}' nie istnieje lub wygasl. Sprawdz ekran robota.",
        )

    return redirect(url_for("control", code=code))


@app.route("/control/<code>")
def control(code: str):
    code = code.upper()
    if not get_session(code):
        return render_template(
            "home.html",
            error=f"Kod '{code}' nie istnieje lub wygasl.",
        )
    return render_template("control.html", code=code)


# ---------------------------------------------------------------------------
# API — diagnostyka
# ---------------------------------------------------------------------------


@app.route("/api/health")
def health():
    try:
        get_redis().ping()
        redis_ok = True
    except Exception as e:
        redis_ok = False
    return jsonify({
        "status": "ok",
        "redis": redis_ok,
        "redis_url": REDIS_URL[:30] + "..." if len(REDIS_URL) > 30 else REDIS_URL,
        "session_ttl": SESSION_TTL_SECONDS,
    })


# ---------------------------------------------------------------------------
# API — ESP32
# ---------------------------------------------------------------------------


@app.route("/api/register", methods=["POST"])
def api_register():
    data = request.json or {}
    device_id = data.get("device_id", "unknown")

    try:
        r = get_redis()
        code = generate_code()
        while get_session(code):
            code = generate_code()

        r.hset(
            session_key(code),
            mapping={
                "device_id": device_id,
                "last_seen": str(time.time()),
                "created_at": str(time.time()),
            },
        )
        r.expire(session_key(code), SESSION_TTL_SECONDS)
        r.delete(queue_key(code))

        print(f"[REGISTER] device={device_id}  code={code}  ttl={SESSION_TTL_SECONDS}s")
        return jsonify({"code": code, "ttl": SESSION_TTL_SECONDS})

    except Exception as e:
        print(f"[REGISTER ERROR] {e}")
        return jsonify({"error": f"Server error: {str(e)}"}), 500


@app.route("/api/poll")
def api_poll():
    code = request.args.get("code", "").upper()
    if not code:
        return jsonify({"error": "Brak kodu"}), 400

    session = get_session(code)
    if not session:
        return jsonify({"error": "Sesja wygasla"}), 404

    try:
        r = get_redis()
        r.hset(session_key(code), "last_seen", str(time.time()))
        refresh_ttl(code)

        qk = queue_key(code)
        events = []
        while True:
            item = r.lpop(qk)
            if item is None:
                break
            events.append(json.loads(item))

        return jsonify(events)

    except Exception as e:
        print(f"[POLL ERROR] {e}")
        return jsonify({"error": str(e)}), 500


# ---------------------------------------------------------------------------
# API — Przeglądarka
# ---------------------------------------------------------------------------


@app.route("/api/command/<code>", methods=["POST"])
def api_command(code: str):
    code = code.upper()
    if not get_session(code):
        return jsonify({"error": "Sesja wygasla"}), 404

    data = request.json or {}
    cmd = data.get("cmd", "S")
    robot_cmd = CMD_MAP.get(cmd, cmd)

    push_event(code, {"event": "robot_command", "data": {"command": robot_cmd, "value": 1}})
    return jsonify({"status": "ok"})


# ---------------------------------------------------------------------------
# API — NVIDIA AI
# ---------------------------------------------------------------------------


def _run_sequence(code: str, sequence: list):
    try:
        for step in sequence:
            cmd = step.get("cmd", "S")
            duration = step.get("time", 0)
            robot_cmd = CMD_MAP.get(cmd, cmd)
            push_event(
                code,
                {"event": "robot_command", "data": {"command": robot_cmd, "value": duration}},
            )
        push_event(code, {"event": "robot_command", "data": {"command": "stop", "value": 0}})
    except Exception as exc:
        print(f"[SEQUENCE ERROR] {exc}")


@app.route("/api/nvidia/<code>", methods=["POST"])
def api_nvidia(code: str):
    code = code.upper()
    if not get_session(code):
        return jsonify({"error": "Sesja wygasla"}), 404

    data = request.json or {}
    user_msg = data.get("prompt", "")

    if not NVIDIA_API_KEY or NVIDIA_API_KEY == "your_key_here":
        return jsonify({"status": "error", "message": "Brak klucza NVIDIA_API_KEY."}), 500

    headers = {
        "Authorization": f"Bearer {NVIDIA_API_KEY}",
        "Content-Type": "application/json",
    }
    payload = {
        "model": "meta/llama-3.1-8b-instruct",
        "messages": [
            {
                "role": "system",
                "content": (
                    "Jestes kontrolerem robota ESP32. Zamien polecenie na JSON - wylacznie tablice akcji. "
                    "Kazda akcja ma 'cmd' (F-przod, B-tyl, L-lewo, R-prawo, S-stop) i 'time' (sekundy). "
                    "Na koncu zawsze dodaj cmd:S, time:0. Zwroc TYLKO czysty JSON bez markdown. "
                    'Przyklad: [{"cmd":"F","time":2},{"cmd":"S","time":0}]'
                ),
            },
            {"role": "user", "content": user_msg},
        ],
        "temperature": 0.1,
        "max_tokens": 1024,
    }

    try:
        resp = requests.post(
            "https://integrate.api.nvidia.com/v1/chat/completions",
            headers=headers,
            json=payload,
            timeout=30,
        )
        resp.raise_for_status()

        reply_text = resp.json()["choices"][0]["message"]["content"].strip()

        for fence in ("```json", "```"):
            if reply_text.startswith(fence):
                reply_text = reply_text[len(fence):]
        if reply_text.endswith("```"):
            reply_text = reply_text[:-3]

        sequence = json.loads(reply_text.strip())
        _run_sequence(code, sequence)

        return jsonify({"status": "success", "message": "Zrozumiano, wykonuje!", "sequence": sequence})

    except Exception as exc:
        error_msg = str(exc)
        if "401" in error_msg:
            error_msg = "Zly klucz do NVIDIA API"
        print(f"[NVIDIA ERROR] {error_msg}")
        return jsonify({"status": "error", "message": error_msg}), 500


# ---------------------------------------------------------------------------
# Uruchomienie lokalne
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
