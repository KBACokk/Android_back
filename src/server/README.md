# server/ — ZeroMQ-сервер приёма телеметрии

Модуль реализует серверную часть приложения: приём JSON-пакетов с телеметрией от Android-устройства через протокол ZeroMQ (REQ/REP).

## Файлы

| Файл | Описание |
|---|---|
| [`server.hpp`](server.hpp) | Заголовочный файл — объявление `run_server()` |
| [`server.cpp`](server.cpp) | Реализация сервера и парсинг JSON-данных |

## Принцип работы

### Запуск сервера — `run_server()`

1. Создаёт ZeroMQ-сокет типа `REP` (ответ на запрос).
2. Привязывается к порту **`tcp://*:7777`**.
3. В бесконечном цикле (пока `running == true`):
   - Принимает сообщение с таймаутом 3 секунды.
   - Немедленно отправляет ответ `"Ok"`.
   - Извлекает JSON из сообщения (ищет первую `{` для обрезки мусора).
   - Определяет тип пакета:
     - **`batch_upload`** — массив измерений в поле `data[]`, каждый элемент обрабатывается отдельно.
     - **Одиночный пакет** — одно измерение.
   - Для каждого измерения:
     - Вызывает `parseJsonData()` для обновления `current_telemetry` (под мьютексом).
     - Вызывает [`saveToDatabase()`](../db/db.hpp) для сохранения в PostgreSQL.
   - Добавляет запись в `log_messages`.

### Парсинг данных — `parseJsonData(const json& j)`

Извлекает из JSON-объекта:

| Поле JSON | Куда записывается | Описание |
|---|---|---|
| `lat`, `lon` | `current_telemetry.lat/lon` | Координаты GPS |
| `accuracy` | `current_telemetry.acc` | Точность позиционирования |
| `networkType` | `current_telemetry.mobile_data` | Тип сети (LTE, NR, GSM) |
| `timestamp` | — | Время измерения (мс), для построения графиков |
| `cells[]` | `current_telemetry.cells` | Массив информации о сотовых вышках |

Для каждой ячейки из `cells[]` заполняется структура `CellInfo` с полями `dbm`, `pci`, `tac`, `rsrp`, `rssi`, `sinr` — в зависимости от типа сети (LTE / NR / GSM).

Данные распределяются по историям:
- `rsrp_history_by_pci` — история RSRP по каждому PCI.
- `rssi_history_by_pci` — история RSSI по каждому PCI.
- `sinr_history_by_pci` — история SINR по каждому PCI.

### Определение основного сигнала — `primary_signal_from_cell()`

Выбирает значение сигнала в зависимости от типа сети:
- **LTE** → `rsrp` (или `dbm` при отсутствии).
- **NR** → `ss_rsrp`.
- **GSM** → `dbm`.

## Связи с другими модулями

- Использует типы из [`osm/types.hpp`](../osm/types.hpp) (`TelemetryData`, `CellInfo`, `SignalHistory`).
- Вызывает [`saveToDatabase()`](../db/db.hpp) для записи в PostgreSQL.
- Глобальные переменные (`current_telemetry`, `mtx`, `running` и др.) определены в [`main.cpp`](../main.cpp).
