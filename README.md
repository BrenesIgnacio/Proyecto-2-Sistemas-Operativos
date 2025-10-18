# Simulador de Paginación (Proyecto 2 SO)

simulador de memoria y estrategias de paginación con interfaz gráfica. Modela procesos, páginas virtuales, una RAM con cantidad fija de marcos y varios algoritmos de reemplazo de páginas. La app está escrita en C y usa GTK+ 3.

## Descripción general

- Simula un MMU simplificado con número fijo de marcos (`RAM_FRAMES = 100`) y tamaño de página (`PAGE_SIZE = 4096`).
- Lleva control de procesos, asignaciones (punteros) y páginas; actualiza métricas como page faults, expulsiones y aciertos.
- Incluye varias políticas de reemplazo: Algoritmo Optimo, FIFO, Segunda Oportunidad (Clock), MRU y Aleatorio.
- Trae un esqueleto de interfaz en GTK (ventana + encabezado). Existen ganchos para visualización, pero la UI es mínima por ahora.

## Funcionalidades

- Núcleo de simulación (`sim_engine.c`) para crear/expulsar/cargar páginas y mantener las tablas de RAM/marcos
- Módulo de algoritmos (`algorithms.c`) con políticas enchufables
- Analizador de instrucciones (`instr_parser.c`) para cargar un script de trabajo, más un generador aleatorio de instrucciones
- Capa de orquestación (`sim_manager.c`) precomputa los eventos de acceso y las colas OPT de "next use"; el bucle de avance sigue en andamiaje
- Andamiaje de UI (`ui_init.c`, `ui_view.c`, `visualization_draw.c`) para mostrar estado y estadísticas
- Configuración por defecto (`config.c`) para demostraciones rápidas

## Estructura del proyecto

```
include/
  algorithms.h         # Hooks del algoritmo y selección de víctima
  common.h             # Tipos/constantes comunes (PAGE_SIZE, RAM_FRAMES, ...)
  config.h             # Configuración de demo y utilidades
  instr_parser.h       # Estructura de instrucción y API de parser/generador
  sim_engine.h         # API del motor de simulación
  sim_manager.h        # Coordinador de alto nivel (manager)
  sim_types.h          # Estructuras base (Page, Frame, MMU, Simulator, ...)
  ui_init.h            # Contexto GTK y arranque
  ui_view.h            # Constructores de UI
  util.h               # Utilidades (xmalloc, logging, rng)
  visualization_draw.h # Dibujo de RAM/estadísticas
src/
  algorithms.c         # FIFO, OPT, Segunda Oportunidad, MRU, Aleatorio
  config.c             # Valores por defecto e impresión
  instr_parser.c       # Implementación del parser/generador
  main.c               # Punto de entrada; arranca la UI GTK
  sim_engine.c         # Núcleo de memoria y paginación
  sim_manager.c        # Manager (andamiaje)
  ui_init.c            # Inicialización de GTK
  ui_view.c            # Ventana principal (UI de ejemplo por ahora)
  util.c               # Utilidades
  visualization_draw.c # Callbacks de dibujo (ganchos)
Makefile               # Compilación con gcc y GTK+ 3
```

## Compilación

Requisitos (Debian):
- gcc, make, pkg-config
- Archivos de desarrollo de GTK+ 3: `libgtk-3-dev`

Instalación rápida (opcional):

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev
```

Compilar el proyecto:

```bash
make
```

Esto genera el ejecutable `pager_sim` en la raíz del repositorio.

## Ejecución

Desde la raíz del proyecto:

```bash
./pager_sim
```

Se abrirá una ventana GTK titulada “Paging Simulator”. La interfaz actual es un esqueleto/maqueta; el motor de simulación y los algoritmos están implementados y listos para conectarse a la UI.

## Formato de instrucciones (workloads)

El simulador puede leer scripts de instrucciones para dirigir asignaciones y accesos. Las líneas no deben tener caracteres extra (los comentarios comienzan con `#`). Operaciones soportadas:

- `new(pid,size)`  Reserva `size` bytes para el proceso `pid`. El parser asigna automáticamente un `ptr_id` comenzando en 1.
- `use(ptr)`       Accede a la asignación referenciada por `ptr` (toca sus páginas).
- `delete(ptr)`    Libera la asignación referenciada por `ptr`.
- `kill(pid)`      Termina el proceso `pid` y libera sus recursos.

Ejemplo:

```
# pid=1 crea ~3 páginas, luego accede y las libera
new(1, 12000)
use(1)
use(1)
delete(1)

# ciclo de vida de pid=2
new(2, 4096)
use(2)
kill(2)
```

## Algoritmos

Políticas disponibles (ver `include/sim_types.h` y `src/algorithms.c`):
- OPT (óptimo de Belady, emplea las colas precalculadas de usos futuros)
- FIFO
- Segunda Oportunidad (Clock)
- MRU (Most Recently Used)
- Aleatorio

El cambio de política se hace asignando `Simulator.algorithm` (enum `AlgorithmType`). El cableado desde la UI/manager está en proceso.

## Configuración

Valores por defecto para demos (`config.c`):
- `seed = 1234`
- `process_count = 10`
- `op_count = 500`
- `algorithm = 1` (por ejemplo, FIFO)

Constantes en `include/common.h`:
- `PAGE_SIZE = 4096`
- `RAM_FRAMES = 100`

## Notas de desarrollo

- Ciclo del motor: `sim_init()` → múltiples `sim_process_instruction()` → `sim_reset()` / `sim_free()`
- Hooks de algoritmo: `algorithms_on_page_loaded/evicted/accessed()` mantienen el estado de la política
- La UI es intencionalmente simple; conecta manager/motor y las funciones de dibujo para visualizar marcos y estadísticas
