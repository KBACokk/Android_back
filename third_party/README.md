# third_party/ - Сторонние библиотеки (Git-подмодули)

Каталог содержит сторонние библиотеки, подключённые как Git-субмодули. Эти библиотеки используются для рендеринга графического интерфейса и графиков.

## Подмодули

| Директория | Библиотека | Назначение | Репозиторий |
|---|---|---|---|
| [`imgui/`](imgui/) | **Dear ImGui** (ветка `docking`) | Immediate Mode GUI - все окна, виджеты и элементы управления | [ocornut/imgui](https://github.com/ocornut/imgui) |
| [`implot/`](implot/) | **ImPlot** | Построение графиков (RSRP, RSSI, SINR) и отображение карты OSM | [epezent/implot](https://github.com/epezent/implot) |
| [`stb/`](stb/) | **stb_image / stb_image_write** | Загрузка и сохранение PNG-изображений (тайлы карты и heatmap) | [nothings/stb](https://github.com/nothings/stb) |

## Используемые файлы из каждой библиотеки

### Dear ImGui

Компилируются в составе проекта (см. [`CMakeLists.txt`](../CMakeLists.txt)):

- `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp` - ядро ImGui.
- `backends/imgui_impl_sdl2.cpp` - бекенд для SDL2 (ввод, окно).
- `backends/imgui_impl_opengl3.cpp` - бекенд для OpenGL 3 (рендеринг).

### ImPlot

- `implot.cpp`, `implot_items.cpp` - ядро библиотеки графиков.

### STB

Подключается как header-only через `#include <stb_image.h>` и `#include <stb_image_write.h>`:

- **stb_image** - декодирование PNG-тайлов в RGBA (используется в [`tile_loader.cpp`](../src/osm/tile_loader.cpp) и [`heatmap.cpp`](../src/osm/heatmap.cpp)).
- **stb_image_write** - сохранение сгенерированных тайлов heatmap в PNG (используется в [`heatmap.cpp`](../src/osm/heatmap.cpp)).

## Обновление подмодулей

```bash
git submodule update --init --recursive
```
