import zmq
import time
from datetime import datetime

LOG_FILE = "server_received_data.txt"
packet_count = 0

print("Сервер запущен на порту 7777")
print("Данные сохраняются в файл:", LOG_FILE)
print("Нажмите Ctrl+C для остановки\n")

context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://0.0.0.0:7777")

try:
    while True:
        message = socket.recv().decode('utf-8')
        packet_count += 1
        
        print(f"[{packet_count}] Получено: {message}")
        
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            time_now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            f.write(f"[{time_now}] {message}\n")
        
        response = f"OK (пакет #{packet_count})"
        socket.send(response.encode('utf-8'))
        

except KeyboardInterrupt:
    print("\n\n Сервер остановлен")
    print(f"Всего получено пакетов: {packet_count}")
    print(f"Данные сохранены в {LOG_FILE}")

finally:
    socket.close()
    context.term()