import time
import threading
import json
import sqlite3
import requests
import cv2
import webbrowser
import os
from flask import Flask, render_template, Response, request, jsonify
from flask_mqtt import Mqtt
from datetime import datetime, timedelta

app = Flask(__name__)

# 1. 設定區
app.config['MQTT_BROKER_URL'] = 'mqttgo.io'
app.config['MQTT_BROKER_PORT'] = 1883
app.config['MQTT_USERNAME'] = '' 
app.config['MQTT_PASSWORD'] = '' 
app.config['MQTT_REFRESH_TIME'] = 1.0 
mqtt = Mqtt(app)

CONFIG_FILE = 'config.json'
DEFAULT_CONFIG = {
    "rtsp_url": "rtsp://10.197.186.146:554/11",
    "telegram_bot_token": "YOUR_TELEGRAM_BOT_TOKEN",
    "telegram_chat_id": "YOUR_CHAT_ID"
}

def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"讀取 Config 失敗: {e}，使用預設值")
    return DEFAULT_CONFIG.copy()

def save_config(new_config):
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(new_config, f, indent=4, ensure_ascii=False)
        return True
    except Exception as e:
        print(f"寫入 Config 失敗: {e}")
        return False

app_config = load_config()
TELEGRAM_BOT_TOKEN = app_config.get('telegram_bot_token', '')
TELEGRAM_CHAT_ID = app_config.get('telegram_chat_id', '')

current_data = {
    'temp': 0, 'tds': 0, 'ph': 0, 'turbidity': 0, 'turbidity_ntu': 0, 'water_level': 0
}

device_status = {'heater': None, 'pump': None}
last_alert_time = {'heater': 0, 'pump': 0, 'water_level': 0, 'tds': 0, 'ph': 0}
ALERT_COOLDOWN = 600 

# 2. 資料庫功能
def init_db():
    conn = sqlite3.connect('fish_system.db')
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS sensor_logs 
                 (timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, 
                  temp REAL, tds REAL, ph REAL, turbidity REAL, turbidity_ntu REAL, water_level REAL)''')
    c.execute('''CREATE TABLE IF NOT EXISTS system_events 
                 (timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, 
                  event_type TEXT, message TEXT)''')
    try:
        c.execute("SELECT turbidity_ntu FROM sensor_logs LIMIT 1")
    except sqlite3.OperationalError:
        print("資料庫遷移: 新增 turbidity_ntu 欄位")
        c.execute("ALTER TABLE sensor_logs ADD COLUMN turbidity_ntu REAL")
    conn.commit()
    conn.close()

def save_to_db(data):
    try:
        conn = sqlite3.connect('fish_system.db')
        c = conn.cursor()
        current_time_str = (datetime.utcnow() + timedelta(hours=8)).strftime('%Y-%m-%d %H:%M:%S')
        c.execute("INSERT INTO sensor_logs (timestamp, temp, tds, ph, turbidity, turbidity_ntu, water_level) VALUES (?, ?, ?, ?, ?, ?, ?)",
                  (current_time_str, data['temp'], data['tds'], data['ph'], data['turbidity'], data.get('turbidity_ntu', 0), data['water_level']))
        conn.commit()
        conn.close()
        # print(f"[DB] 數據已寫入 ({current_time_str})")
    except Exception as e:
        print(f"資料庫錯誤: {e}")

def log_event(event_type, message):
    try:
        conn = sqlite3.connect('fish_system.db')
        c = conn.cursor()
        current_time_str = (datetime.utcnow() + timedelta(hours=8)).strftime('%Y-%m-%d %H:%M:%S')
        c.execute("INSERT INTO system_events (timestamp, event_type, message) VALUES (?, ?, ?)",
                  (current_time_str, event_type, message))
        conn.commit()
        conn.close()
        print(f"[Event Logged] {event_type}: {message}")
    except Exception:
        pass

def send_telegram_alert(message):
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID: return
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    threading.Thread(target=lambda: requests.post(url, data={'chat_id': TELEGRAM_CHAT_ID, 'text': message}, timeout=2.0)).start()

# 3. 邏輯控制 (僅負責警報，不負責控制)
def check_logic(data):
    global last_alert_time
    current_time = time.time()

    # 1. 水溫警報
    if data['temp'] < 20 and data['temp'] > 5:
        if current_time - last_alert_time['heater'] > ALERT_COOLDOWN:
            send_telegram_alert(f"【低溫警報】水溫：{data['temp']}°C\n系統偵測到低溫，已開啟加熱棒")
            last_alert_time['heater'] = current_time 
    
    # 2. 水質警報
    ntu = data.get('turbidity_ntu', 0)
    tds = data.get('tds', 0)
    pump_needed = False
    
    if ntu >= 3000: pump_needed = True
    if tds > 200: pump_needed = True

    if pump_needed:
        if current_time - last_alert_time['pump'] > ALERT_COOLDOWN:
            send_telegram_alert(f"【水質異常】NTU:{ntu}, TDS:{tds}\n系統偵測到水質髒污，已開啟過濾")
            last_alert_time['pump'] = current_time

    # 3. 水位警報
    if data['water_level'] < 410 and data['water_level'] >= 0:
        if current_time - last_alert_time['water_level'] > ALERT_COOLDOWN:
            send_telegram_alert(f"【水位危險】數值：{data['water_level']}\n請盡快補水！")
            log_event('ALARM', f"水位過低 ({data['water_level']})")
            last_alert_time['water_level'] = current_time

    # 4. pH 警報 (新增)
    ph_val = data.get('ph', 7.0)
    if ph_val < 6.5 or ph_val > 8.5:
        if current_time - last_alert_time['ph'] > ALERT_COOLDOWN:
            send_telegram_alert(f"【pH 水質警報】pH：{ph_val}\n數值超出安全範圍 (6.5 ~ 8.5)")
            log_event('ALARM', f"pH 異常 ({ph_val})")
            last_alert_time['ph'] = current_time


# 4. MQTT 訊息處理

@mqtt.on_connect()
def handle_connect(client, userdata, flags, rc):
    print("Python MQTT Connected")
    client.subscribe('ttu_fish/sensors')
    # [新增] 訂閱日誌頻道
    client.subscribe('ttu_fish/log')

@mqtt.on_message()
def handle_mqtt_message(client, userdata, message):
    global current_data
    
    # Case A: 感測數據
    if message.topic == 'ttu_fish/sensors':
        try:
            payload = message.payload.decode()
            data = json.loads(payload)
            
            current_data['temp'] = float(data.get('temp', 0))
            current_data['ph'] = float(data.get('ph', 0))
            current_data['tds'] = float(data.get('tds', 0))
            current_data['turbidity'] = float(data.get('turbidity', 0)) 
            current_data['turbidity_ntu'] = int(data.get('ntu', 0))     
            current_data['water_level'] = int(data.get('level', 0))     
            
            # print(f"[MQTT Sync] {current_data}")
            save_to_db(current_data)
            check_logic(current_data)
            
        except Exception as e:
            print(f"MQTT JSON 解析錯誤: {e}")
            
    # Case B: [新增] 來自 ESP32 的事件日誌
    elif message.topic == 'ttu_fish/log':
        try:
            payload = message.payload.decode()
            log_data = json.loads(payload)
            
            event_type = log_data.get('event_type', 'INFO')
            msg = log_data.get('message', '')
            
            # 寫入資料庫，供網頁讀取
            log_event(event_type, msg)
            
        except Exception as e:
            print(f"MQTT Log Error: {e}")

# 5. 影像串流
RTSP_URL = app_config.get('rtsp_url', DEFAULT_CONFIG['rtsp_url'])

class VideoCamera(object):
    def __init__(self):
        self.video = cv2.VideoCapture(RTSP_URL)
        self.video.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.frame = None
        self.lock = threading.Lock()
        self.running = True
        self.thread = threading.Thread(target=self.update)
        self.thread.daemon = True
        self.thread.start()

    def __del__(self):
        self.running = False
        if self.video.isOpened(): self.video.release()

    def update(self):
        while self.running:
            success, image = self.video.read()
            if success:
                with self.lock: self.frame = image
            else: time.sleep(0.1)
            time.sleep(0.01)

    def get_frame(self):
        with self.lock:
            if self.frame is not None:
                ret, jpeg = cv2.imencode('.jpg', self.frame, [int(cv2.IMWRITE_JPEG_QUALITY), 80])
                if ret: return jpeg.tobytes()
        return None

camera = None
def generate_frames():
    global camera
    if camera is None: camera = VideoCamera()
    while True:
        frame = camera.get_frame()
        if frame:
            yield (b'--frame\r\n' b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
        
        time.sleep(0.05) # 約 20 FPS

@app.route('/video_feed')
def video_feed():
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

# 6. Flask 路由
@app.route('/')
def index():
    return render_template('index.html', data=current_data)

@app.route('/api/data')
def get_data():
    return json.dumps(current_data)

@app.route('/api/logs')
def get_logs():
    try:
        conn = sqlite3.connect('fish_system.db')
        conn.row_factory = sqlite3.Row
        c = conn.cursor()
        # 取得最新的 20 筆紀錄
        c.execute("SELECT timestamp, event_type, message FROM system_events ORDER BY timestamp DESC LIMIT 20")
        rows = c.fetchall()
        conn.close()
        return jsonify([dict(row) for row in rows])
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/log_event', methods=['POST'])
def api_log_event():
    try:
        data = request.json
        log_event(data.get('event_type', 'INFO'), data.get('message', ''))
        return jsonify({'status': 'success'})
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/settings', methods=['GET', 'POST'])
def handle_settings():
    global app_config, RTSP_URL, TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID
    if request.method == 'GET':
        return jsonify(app_config)
    if request.method == 'POST':
        app_config.update(request.json)
        if save_config(app_config):
            RTSP_URL = app_config.get('rtsp_url', RTSP_URL)
            TELEGRAM_BOT_TOKEN = app_config.get('telegram_bot_token', TELEGRAM_BOT_TOKEN)
            TELEGRAM_CHAT_ID = app_config.get('telegram_chat_id', TELEGRAM_CHAT_ID)
            return jsonify({'status': 'success', 'message': '設定已儲存並更新'})
        return jsonify({'status': 'error', 'message': '寫入設定檔失敗'}), 500

if __name__ == '__main__':
    init_db()
    
    def open_browser():
        time.sleep(1.5)
        webbrowser.open('http://localhost:5000')

    threading.Thread(target=open_browser).start()
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)