import os
import json
import time
import random
import string
import threading
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

# ---------------------------------------------------------------------------
# Redis
# ---------------------------------------------------------------------------

REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379")
r = redis.from_url(REDIS_URL, decode_responses=True)

# ---------------------------------------------------------------------------
# Pomocnicze funkcje
# ---------------------------------------------------------------------------

_CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # bez mylących O/0, I/1


def generate_code() -> str:
    """Generuje unikalny kod parowania w formacie XXX-XXX."""
    part1 = "".join(random.choices(_CODE_CHARS, k=3))
    part2 = "".join(random.choices(_CODE_CHARS, k=3))
    return f"{part1}-{part2}"


def session_key(code: str) -> str:
    return f"robot:session:{code}"


def queue_key(code: str) -> str:
    return f"robot:queue:{code}"


def get_session(code: str):
    data = r.hgetall(session_key(code))
    return data if data else None


def refresh_ttl(code: str):
    r.expire(session_key(code), SESSION_TTL_SECONDS)
    r.expire(queue_key(code), SESSION_TTL_SECONDS)


def push_event(code: str, event: dict):

    r.rpush(queue_key(code), json.dumps(event))
    r.expire(queue_key(code), SESSION_TTL_SECONDS)


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
            error=f"Kod „{code}"
        )

    return redirect(url_for("control", code=code))


@app.route("/control/<code>")
def control(code: str):
    code = code.upper()
    if not get_session(code):
        return render_template(
            "home.html",
            error=f"Kod „{code}" #nie istnieje lub wygasł.",
        )
    return render_template("control.html", code=code)



@app.route("/api/register", methods=["POST"])
def api_register():
    """ESP32 rejestruje się i dostaje kod parowania."""
    data = request.json or {}
    device_id = data.get("device_id", "unknown")

    # Generuj unikalny kod
    code = generate_code()
    while get_session(code):
        code = generate_code()

    # Zapisz sesję w Redis
    r.hset(
        session_key(code),
        mapping={
            "device_id": device_id,
            "last_seen": str(time.time()),
            "created_at": str(time.time()),
        },
    )
    r.expire(session_key(code), SESSION_TTL_SECONDS)

    # Wyczyść (lub utwórz) kolejkę
    r.delete(queue_key(code))
    r.expire(queue_key(code), SESSION_TTL_SECONDS)

    print(f"[REGISTER] device={device_id}  code={code}  ttl={SESSION_TTL_SECONDS}s")
    return jsonify({"code": code, "ttl": SESSION_TTL_SECONDS})


@app.route("/api/poll")
def api_poll():

    code = request.args.get("code", "").upper()
    if not code:
        return jsonify({"error": "Brak kodu"}), 400

    session = get_session(code)
    if not session:
        return jsonify({"error": "Sesja wygasła"}), 404


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


# ---------------------------------------------------------------------------
# API — Przeglądarka
# ---------------------------------------------------------------------------


@app.route("/api/command/<code>", methods=["POST"])
def api_command(code: str):

    code = code.upper()
    if not get_session(code):
        return jsonify({"error": "Sesja wygasła"}), 404

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
        # Upewnij się, że robot zatrzymuje się na końcu
        push_event(code, {"event": "robot_command", "data": {"command": "stop", "value": 0}})
    except Exception as exc:
        print(f"[SEQUENCE ERROR] {exc}")


@app.route("/api/nvidia/<code>", methods=["POST"])
def api_nvidia(code: str):
    code = code.upper()
    if not get_session(code):
        return jsonify({"error": "Sesja wygasła"}), 404

    data = request.json or {}
    user_msg = data.get("prompt", "")

    if not NVIDIA_API_KEY or NVIDIA_API_KEY == "your_key_here":
        return jsonify({"status": "error", "message": "Brak klucza NVIDIA_API_KEY w zmiennych środowiskowych."}), 500

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
                    "Jesteś kontrolerem robota na platformie ESP32. Zamień polecenie użytkownika na format JSON "
                    "- wyłącznie tablicę akcji. Każda akcja ma 'cmd' (F-przód, B-tył, L-lewo, R-prawo, S-stop) "
                    "oraz 'time' (czas w sekundach jak np 1.5 lub 2). Przeanalizuj uważnie czas z języka naturalnego. "
                    "Pamiętaj by zawsze na koniec zatrzymać dodając cmd:S, time:0. Zwróć TYLKO czysty i poprawny JSON, "
                    'absolutnie bez żadnych dodatkowych opisów, bez formatowania markdown. '
                    'Przykładowy poprawny output: [{"cmd":"F","time":2},{"cmd":"R","time":1},{"cmd":"S","time":0}]'
                ),
            },
            {"role": "user", "content": user_msg},
        ],
        "temperature": 0.1,
        "max_tokens": 1024,
    }

    try:
        print(f"[NVIDIA] Wysyłanie: {user_msg}")
        resp = requests.post(
            "https://integrate.api.nvidia.com/v1/chat/completions",
            headers=headers,
            json=payload,
            timeout=30,
        )
        resp.raise_for_status()

        reply_text = resp.json()["choices"][0]["message"]["content"].strip()
        print(f"[NVIDIA] Odpowiedź:\n{reply_text}")

        # Wyczyść markdown code fences jeśli model je dodał
        if reply_text.startswith("```json"):
            reply_text = reply_text[7:]
        if reply_text.startswith("```"):
            reply_text = reply_text[3:]
        if reply_text.endswith("```"):
            reply_text = reply_text[:-3]

        sequence = json.loads(reply_text.strip())

        # Zakolejkuj komendy (bez blokowania wątku — działa na Vercel)
        _run_sequence(code, sequence)

        return jsonify({"status": "success", "message": "Zrozumiano, wykonuję!", "sequence": sequence})

    except Exception as exc:
        error_msg = str(exc)
        if "401" in error_msg:
            error_msg = "Zły klucz do NVIDIA API"
        print(f"[NVIDIA ERROR] {error_msg}")
        return jsonify({"status": "error", "message": error_msg}), 500


# ---------------------------------------------------------------------------
# Uruchomienie lokalne
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
