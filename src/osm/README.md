# osm/ — Модуль карты OpenStreetMap и тепловых карт

Модуль проекта. Отвечает за:
- Отображение интерактивной карты OpenStreetMap (тайлы) через ImPlot.
- Загрузку и кэширование тайлов карты (HTTP + файловый кэш).
- Генерацию и отрисовку тепловых карт (heatmap) по данным сигнала.
- Математические преобразования координат (проекция Меркатора).

## Файлы

### Типы данных и проекция

| Файл | Описание |
|---|---|
| [`types.hpp`](types.hpp) | Структуры `TelemetryData`, `CellInfo`, `SignalHistory` — общие типы для всего приложения |
| [`osm_projection.hpp`](osm_projection.hpp) | Математика проекции Меркатора — преобразования координат ↔ тайлов |

### Карта (OSM)

| Файл | Описание |
|---|---|
| [`osm_map.hpp`](osm_map.hpp) | Заголовочный файл окна карты |
| [`osm_map.cpp`](osm_map.cpp) | Главное окно карты ImPlot: рендеринг тайлов, heatmap-слой, легенда, управление масштабом |

### Тепловая карта (Heatmap)

| Файл | Описание |
|---|---|
| [`heatmap.hpp`](heatmap.hpp) | Критерии, состояние генерации, палитра |
| [`heatmap.cpp`](heatmap.cpp) | Генерация PNG-тайлов heatmap, управление GPU-кэшем, цветовая палитра |

### Загрузка и кэширование тайлов

| Файл | Описание |
|---|---|
| [`tile_loader.hpp`](tile_loader.hpp) | API фоновой загрузки тайлов |
| [`tile_loader.cpp`](tile_loader.cpp) | Очередь загрузки, фоновый поток (worker), скачивание с OSM + декодирование PNG |
| [`tile_cache.hpp`](tile_cache.hpp) | API файлового кэша тайлов |
| [`tile_cache.cpp`](tile_cache.cpp) | Чтение / запись PNG-файлов тайлов на диск |
| [`tile_paths.hpp`](tile_paths.hpp) | API формирования путей к файлам тайлов |
| [`tile_paths.cpp`](tile_paths.cpp) | Формирование пути `cache/{zoom}/{x}/{y}.png` |
| [`tile_texture.hpp`](tile_texture.hpp) | Структура `TileTexture` — OpenGL-текстура тайла |
| [`tile_texture.cpp`](tile_texture.cpp) | Загрузка RGBA-данных в OpenGL-текстуру (`glTexImage2D`) |

### HTTP-клиент

| Файл | Описание |
|---|---|
| [`curl_func.hpp`](curl_func.hpp) | API HTTP-загрузки |
| [`curl_func.cpp`](curl_func.cpp) | Скачивание бинарных данных через libcurl (GET-запрос с User-Agent) |

---

## Подробное описание компонентов

### Типы данных — [`types.hpp`](types.hpp)

Определяет центральные структуры проекта:

- **`SignalHistory`** — кольцевой буфер на 500 точек `(time, value)` для графиков.
- **`CellInfo`** — данные одной сотовой вышки: тип, dbm, pci, tac, rsrp, rssi, sinr.
- **`TelemetryData`** — агрегированное состояние телеметрии:
  - Координаты, тип сети, уровень сигнала.
  - Массивы точек для карты (`map_points_lon`, `map_points_merc_y`).
  - Истории метрик по PCI (`rsrp_history_by_pci`, `rssi_history_by_pci`, `sinr_history_by_pci`).
  - Функция `get_or_init_relative_time_sec()` — вычисление времени для полученных пакетов.

Также объявляет глобальные переменные (`extern`): `current_telemetry`, `mtx`, `log_messages`, `running`, `packet_counter`.

### Окно карты — [`osm_map.cpp`](osm_map.cpp)

Функция `osm_map_render_window()` рисует окно ImGui с ImPlot-графиком, на котором:

1. **Тайлы OSM** — рассчитываются видимые тайлы, запрашиваются через [`tile_loader`](tile_loader.hpp), отрисовываются как `ImPlot::PlotImage`.
2. **Тепловая карта** — поверх тайлов накладываются полупрозрачные PNG heatmap.
3. **Точки телеметрии** — все загруженные точки отображаются как scatter plot (`"All metrics"`).
4. **Легенда heatmap** — цветовая шкала с подписями значений (RSRP, RSRQ, RSSI, Altitude).

Элементы управления:
- Комбобокс `Heatmap criterion` — выбор критерия (RSRP / RSRQ / RSSI / Altitude).
- Слайдер `Zoom` — уровень масштабирования (3–17).
- Чекбокс `Show heatmap layer` — показ/скрытие тепловой карты.
- Прокрутка мышью — масштабирование карты.

### Тепловая карта — [`heatmap.cpp`](heatmap.cpp)

Основные возможности:

- **Генерация PNG-тайлов** — для каждого тайла на уровнях zoom 10–17:
  - Каждая точка данных растеризуется как Гауссово пятно (splat) радиусом 20 пикселей.
  - Значение в каждом пикселе — средневзвешенное по сэмплам (kernel density estimation).
  - Цвет определяется по палитре (синий → голубой → зелёный → жёлтый → красный).
  - Результат сохраняется в PNG 256×256.

- **Цветовая палитра** — `heatmap_palette_rgb()` — 5 стопов (синий → красный).

### Загрузчик тайлов — [`tile_loader.cpp`](tile_loader.cpp)

Фоновый поток (`worker_loop`) для загрузки тайлов OpenStreetMap:

1. Извлекает задачу из очереди (с блокировкой через `condition_variable`).
2. Проверяет файловый кэш ([`tile_read_file_bytes()`](tile_cache.hpp)).
3. Если тайл не найден — скачивает с `https://a.tile.openstreetmap.org/{z}/{x}/{y}.png` через [`curl_http_get_binary()`](curl_func.hpp).
4. Сохраняет в файловый кэш ([`tile_write_file_bytes()`](tile_cache.hpp)).
5. Декодирует PNG → RGBA через `stb_image`.
6. Помещает результат в очередь завершённых задач (`g_done`).
