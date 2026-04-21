from flask import Flask, render_template, request, jsonify
from flask_sock import Sock
import threading
import time
import os
import requests
import socket
from dotenv import load_dotenv

load_dotenv()

def udp_beacon():
    """Rozsyła w sieci informację o serwerze (Broadcast UDP) co 2 sekundy"""
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    while True:
        try:
            udp_socket.sendto(b"ROBOT_SERVER", ("<broadcast>", 5555))
        except Exception:
            pass
        time.sleep(2)

app = Flask(__name__)
sock = Sock(app)

event_queue = []

@app.route('/api/poll')
def poll():
    global event_queue
    events = event_queue.copy()
    event_queue.clear()
    
    return jsonify(events)

@app.route('/')
def index():
    return render_template('index.html')

def execute_sequence(sequence):
    """
    Parsuje i wykonuje sekwencję na podstawie JSONa z LLM.
    Przykładowy sequence: [{"cmd": "F", "time": 2}, {"cmd": "L", "time": 1}]
    """
    print(f"Rozpoczynamy zautomatyzowaną sekwencję AI: {sequence}")
    try:
        for step in sequence:
            cmd = step.get("cmd", "S")
            duration = step.get("time", 0)
            
            print(f"[AI SEQUENCE] Kolejkuję komendę: {cmd} na czas {duration}s")
            cmd_map = {"F": "przód", "B": "tył", "L": "lewo", "R": "prawo", "S": "stop", "W": "wyprostuj"}
            robot_cmd = cmd_map.get(cmd, cmd)
            
            event_queue.append({
                "event": "robot_command",
                "data": {"command": robot_cmd, "value": duration}
            })
            
            if duration > 0:
                time.sleep(duration)
            
        # Na koniec zatrzymaj
        event_queue.append({
            "event": "robot_command",
            "data": {"command": "stop", "value": 0}
        })
        print("[AI SEQUENCE] Zakończono.")
    except Exception as e:
        print(f"Error executing sequence: {e}")


@app.route('/api/nvidia', methods=['POST'])
def nvidia_prompt():
    data = request.json
    user_msg = data.get("prompt", "")
    
    api_key = os.environ.get("NVIDIA_API_KEY")
    if not api_key or api_key == "your_key_here":
        return jsonify({"status": "error", "message": "Brak klucza NVIDIA_API_KEY w pliku .env (podmień 'your_key_here' na realny klucz)."}), 500

    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json"
    }

    # Używamy nowszego modelu meta/llama-3.1-8b-instruct (poprzedni zwrócił 410 Gone)
    payload = {
        "model": "meta/llama-3.1-8b-instruct",
        "messages": [
            {
                "role": "system",
                "content": "Jesteś kontrolerem robota na platformie ESP32. Zamień polecenie użytkownika na format JSON - wyłącznie tablicę akcji. Każda akcja ma 'cmd' (F-przód, B-tył, L-lewo, R-prawo, S-stop) oraz 'time' (czas w sekundach jak np 1.5 lub 2). Przeanalizuj uważnie czas z języka naturalnego. Pamiętaj by zawsze na koniec zatrzymać dodając cmd:S, time:0. Zwróć TYLKO czysty i poprawny JSON, absolutnie bez żadnych dodatkowych opisów, bez formatowania markdown. Przykładowy poprawny output: [{\"cmd\":\"F\",\"time\":2},{\"cmd\":\"R\",\"time\":1},{\"cmd\":\"S\",\"time\":0}]"
            },
            {
                "role": "user",
                "content": user_msg
            }
        ],
        "temperature": 0.1,
        "max_tokens": 1024
    }

    try:
        # Odpytywanie NVIDIA API NIM
        print(f"Wysyłanie do NVIDIA: {user_msg}")
        resp = requests.post("https://integrate.api.nvidia.com/v1/chat/completions", headers=headers, json=payload)
        resp.raise_for_status()
        
        reply_text = resp.json()["choices"][0]["message"]["content"].strip()
        print(f"NVIDIA API Odpowiada:\n{reply_text}")
        
        # Oczyszczenie z markdowna na wypadek "```json"
        if reply_text.startswith("```json"):
            reply_text = reply_text[7:]
        if reply_text.endswith("```"):
            reply_text = reply_text[:-3]
        
        import json
        sequence = json.loads(reply_text.strip())
        
        # Odpal wątek żeby nie blokować odpowiedzi serwera na ten HTTP POST
        t = threading.Thread(target=execute_sequence, args=(sequence,))
        t.start()
        
        return jsonify({"status": "success", "message": "Zrozumiano, wykonuję!", "sequence": sequence})
    except Exception as e:
        error_msg = str(e)
        if "401" in error_msg:
            error_msg = "Zły klucz do NVIDIA API"
        print("API Error:", error_msg)
        return jsonify({"status": "error", "message": error_msg}), 500

@sock.route('/ws_client')
def ws_client(ws):
    print("Podłączono Klienta Sterującego (Telefon/Przeglądarka)")
    try:
        while True:
            data = ws.receive()
            if data:
                cmd_map = {"F": "przód", "B": "tył", "L": "lewo", "R": "prawo", "S": "stop", "W": "wyprostuj"}
                robot_cmd = cmd_map.get(data, data)
                event_queue.append({
                    "event": "robot_command",
                    "data": {"command": robot_cmd, "value": 1}
                })
    except Exception:
        pass
    finally:
        print("Rozłączono Klienta Sterującego")

if __name__ == '__main__':
    # Uruchamiamy wątek rozgłaszający IP laptopa
    beacon_thread = threading.Thread(target=udp_beacon, daemon=True)
    beacon_thread.start()
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)
