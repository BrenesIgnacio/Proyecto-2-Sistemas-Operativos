# Simulador de Paginación (Proyecto 2 SO)

Simulador interactivo de memoria y estrategias de paginación con interfaz gráfica completa. Modela procesos, páginas virtuales, una RAM con cantidad fija de marcos y varios algoritmos de reemplazo de páginas. Permite comparar en tiempo real el desempeño del algoritmo óptimo (OPT) contra el algoritmo seleccionado por el usuario. La app está escrita en C y usa GTK+ 3.

## Descripción general

- **Simulación**: Se corren simultáneamente dos simuladores independientes (OPT y el algoritmo elegido) para comparación directa de rendimiento.
- Simula un MMU simplificado con número fijo de marcos (`RAM_FRAMES = 100`) y tamaño de página (`PAGE_SIZE = 4096`).
- Lleva control de procesos, asignaciones (punteros) y páginas; actualiza métricas como page faults, expulsiones y aciertos.
- Varios algoritmos: **Algoritmo Óptimo (OPT)**, **FIFO**, **Segunda Oportunidad (Clock)**, **LRU**, **MRU** y **Aleatorio**.
- **GUI** con controles de reproducción (Iniciar, Pausar, Step, Reset), selector de algoritmo y visualización de estadísticas en tiempo real.
- **Preprocesamiento**: Analiza todas las instrucciones antes de la simulación para construir el dataset de usos futuros (OPT) y optimizar la ejecución.

## Funcionalidades

- **Núcleo de simulación** (`sim_engine.c`): Crea, expulsa y carga páginas; mantiene las tablas de RAM y marcos físicos.
- **Gestión completa de memoria**: Asignación dinámica de punteros, páginas y procesos con seguimiento de fragmentación interna.
- **Manejo de page faults**: Se detectan y gestionan fallos de página, ejecutando el algoritmo de reemplazo correspondiente.

### Administrador de Simulación (SimManager)
- **Preprocesamiento de la carga** (`sim_manager.c`): Analiza todas las instrucciones antes de ejecutarlas para determinar qué páginas se accederán y cuándo.
- **Dataset de usos futuros**: Se construye una tabla completa de accesos futuros para cada página, permitiendo al algoritmo OPT tomar las decisiones óptimas.
- **Ejecución dual**: Se corre cada instrucción simultáneamente en dos simuladores independientes (OPT y usuario) para comparar.
- **Caché de eventos**: Mapea cada instrucción a sus eventos de acceso a páginas mediante un array de offsets para búsqueda O(1).

### Algoritmos de Reemplazo
- **Módulo de algoritmos** (`algorithms.c`).
- **OPT (Óptimo)**: Es una implementacion del algoritmo de Belady usando colas de usos futuros precalculadas.
- **FIFO**
- **Segunda Oportunidad (Clock)**: Variante de FIFO con bit de referencia y puntero circular.
- **MRU (Most Recently Used)**: Expulsa la página accedida más recientemente.
- **Aleatorio**

### GUI
- **Ventana principal** (`ui_view.c`):
  - **Generar carga**: Crea automáticamente un carga de trabajo aleatorio según parámetros.
  - **Selector de algoritmo**
  - **Iniciar**
  - **Pausar/Continuar**: Detiene y reanuda la ejecución.
  - **Step**: Avanza la simulación una instrucción a la vez.
  - **Reset**: Reinicia los simuladores.
- **Paneles de estadísticas**: Visualización  de métricas de OPT y el algoritmo usuario.
- **Barra**: Muestra progreso actual, tiempo de reloj de cada simulador y nombre del algoritmo.
- **Actualización**: Las métricas se refrescan automáticamente después de cada paso de simulación.

### Sistema de Estadísticas
- **Visualización** (`visualization_draw.c`): Actualiza 14 métricas por simulador:
  - Nombre del simulador y algoritmo usado
  - Reloj de simulación 
  - Tiempo en thrashing
  - Páginas en swap
  - Total de instrucciones procesadas
  - Page faults y page hits
  - Páginas creadas y expulsadas
  - Asignaciones y liberaciones de punteros
  - Bytes solicitados y fragmentación interna

### Analizador de Instrucciones
- **Parser** (`instr_parser.c`): Lee scripts con validación completa de sintaxis y semántica.
- **Generador aleatorio**: Crea sets de instrucciones con distribución configurable de operaciones (new/use/delete/kill).
- **Exportación**: Permite guardar secuencias generadas para reproducibilidad.

## Estructura del proyecto

```
include/
  algorithms.h         # Hooks del algoritmo y selección de víctima
  common.h             # Tipos/constantes comunes (PAGE_SIZE, RAM_FRAMES, ...)
  config.h             # Configuración de demo y utilidades
  instr_parser.h       # Estructura de instrucción y API de parser/generador
  sim_engine.h         # API del motor de simulación (init/reset/free/process_instruction)
  sim_manager.h        # Coordinador de alto nivel: preprocesamiento, eventos, dataset OPT
  sim_types.h          # Estructuras base (Page, Frame, MMU, Simulator, FutureUseDataset, ...)
  ui_init.h            # Contexto GTK, estados de ejecución (RunState) y arranque
  ui_view.h            # Constructores de ventanas y paneles
  util.h               # Utilidades (xmalloc, logging, rng)
  visualization_draw.h # Actualización de labels de estadísticas
src/
  algorithms.c         # Implementación de FIFO, OPT, Segunda Oportunidad, MRU, Random
  config.c             # Valores por defecto e impresión de configuración
  instr_parser.c       # Parser de scripts, generador aleatorio, exportador
  main.c               # Punto de entrada; arranca la UI GTK
  sim_engine.c         # Núcleo completo: MMU, procesos, páginas, page faults, eviction
  sim_manager.c        # Preprocesamiento de carga de trabajo, eventos, dataset OPT, ejecución dual
  ui_init.c            # Inicialización de GTK (mínima)
  ui_view.c            # Ventana principal completa con controles y callbacks
  util.c               # Implementación de utilidades
  visualization_draw.c # Actualización de paneles de estadísticas
Makefile               # Compilación con gcc y GTK+ 3
```

### Descripción de Archivos Clave

**`sim_manager.c`**:
- `precompute_events()`: Analiza todas las instrucciones y construye el array de `AccessEvent` con cada acceso a página.
- `build_future_dataset()`: Crea el dataset de usos futuros para OPT invirtiendo la lista de eventos.
- `sim_manager_init()`: Preprocesa el carga de trabajo y crea dos simuladores independientes.
- `sim_manager_step()`: Ejecuta una instrucción en ambos simuladores simultáneamente.

**`sim_engine.c`**:
- `acquire_frame()`: Obtiene un marco libre; si no hay, ejecuta el algoritmo de reemplazo.
- `handle_new/use/delete/kill()`: Procesadores específicos para cada tipo de instrucción.
- `sim_process_instruction()`: Dispatcher principal que actualiza estadísticas y llama al handler correspondiente.

**`ui_view.c`**:
- `create_stats_grid()`: Genera grillas de 14 métricas con labels dinámicos.
- `on_start/pause/step/reset_clicked()`: Callbacks de botones que gestionan estados de ejecución.
- `tick_simulation()`: Temporizador GTK que avanza la simulación continuamente.
- `ensure_manager_config()`: Verifica que el manager esté inicializado con el algoritmo correcto.

**`algorithms.c`**:
- `fifo_choose()`: Cola circular de páginas cargadas.
- `sc_choose()`: Clock hand con segunda oportunidad.
- `opt_choose()`: Busca la página con uso más lejano en el futuro.
- `lru_choose()`: Expulsa la página con timestamp más antiguo.
- `mru_choose()`: Expulsa la página con timestamp más reciente.
- `rnd_choose()`: Selección aleatoria uniforme.

## Compilación

Requisitos:
- gcc, make, pkg-config
- Archivos de desarrollo de GTK3: `libgtk-3-dev`

Compilar el proyecto:

```bash
make
```

se genera el ejecutable `pager_sim`.

## Ejecución

```bash
./pager_sim
```

## Formato de instrucciones (carga de trabajos)

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

### OPT (Óptimo de Belady)
- **Descripción**: Siempre expulsa la página que no se usará por más tiempo en el futuro.
- **Implementación**: Emplea las colas de usos futuros precalculadas por el `SimManager` durante el preprocesamiento.
- **Ventaja**: Mínimo número teórico de page faults. Sirve como referencia para evaluar otros algoritmos.
- **Limitación**: Requiere conocimiento futuro.

### Aleatorio (Random)
- **Descripción**: Selecciona una página víctima al azar entre las que están en RAM.
- **Implementación**: Usa generador pseudoaleatorio con semilla configurable.
- **Ventaja**: Muy simple, sin overhead de mantenimiento de estado.
- **Desventaja**: Resultados impredecibles; puede expulsar páginas críticas.

## Configuración

Valores por defecto para demos (`config.c`):
- `seed = 1234`
- `process_count = 10` (número de procesos a generar)
- `op_count = 500` (operaciones new/use/delete antes de los kills finales)
- `algorithm = 1` (FIFO por defecto)

Constantes del sistema en `include/common.h`:
- `PAGE_SIZE = 4096` bytes (4 KB por página)
- `RAM_FRAMES = 100` marcos físicos (400 KB de RAM total)

### Parámetros del Generador de carga de trabajo
El generador aleatorio crea instrucciones con la siguiente distribución:
- Cada proceso recibe al menos una asignación inicial (`new`).
- Probabilidades por operación (ajustables en `instr_parser.c`):
  - **35-45%** `new()`: Nuevas asignaciones de 1-20,000 bytes.
  - **40-45%** `use()`: Accesos a punteros existentes.
  - **10-20%** `delete()`: Liberación de punteros.
- Todos los procesos terminan con `kill(pid)` al final del carga de trabajo.

## Características del programa (para el paper)

### Optimizaciones de Rendimiento
- **Preprocesamiento de carga de trabajo**: Analiza todo el script una sola vez; los simuladores no repiten este trabajo.
- **Arrays dinámicos con crecimiento exponencial**: Todas las tablas (páginas, procesos, eventos) duplican capacidad al crecer, minimizando reasignaciones.
- **Offsets de eventos**: Mapeo O(1) de instrucción→eventos; evita búsquedas lineales durante la ejecución.
- **Lazy eviction**: Solo se expulsan páginas cuando se necesita un marco libre (no se escanea toda la RAM innecesariamente).

### Gestión de Memoria
- **Tablas dispersas**: Las tablas de páginas/procesos/punteros se indexan directamente por ID, permitiendo acceso O(1).
- **Swap-and-pop**: Eliminación de elementos en O(1) moviendo el último al slot liberado.
- **Shrinking de futuras**: Después de construir el dataset OPT, se liberan entradas vacías y se ajusta capacidad al tamaño real.
- **Detección de fugas**: El ciclo `sim_clear_state` recorre todas las tablas liberando recursos correctamente.

### Métricas Avanzadas
- **Fragmentación interna**: Calcula bytes desperdiciados por redondeo a múltiplos de `PAGE_SIZE`.
- **Thrashing time**: Acumula ciclos donde el sistema pasa más tiempo en page faults que en trabajo útil.
- **Páginas en swap**: Contador de páginas expulsadas actualmente fuera de RAM.

## Estados 

Hay cuatro estados (`RunState` en `ui_init.h`):

1. **IDLE**: Sin simulación cargada o completada. Permite generar carga y seleccionar algoritmo.
2. **RUNNING**: Simulación en ejecución continua con temporizador activo (~25 pasos/segundo).
3. **PAUSED**: Simulación detenida pero con estado preservado. Botón "Continuar" permite reanudar.
4. **STEP**: Modo manual; cada clic en "Step" avanza exactamente una instrucción.

#### Componentes Clave

**SimManager** (`sim_manager.c`):
- Lleva dos simuladores en paralelo.
- Precomputa todos los accesos a páginas antes de la ejecución.
- Mantiene la posición actual en el carga de trabajo y sincroniza ambos simuladores.

**Simulator** (`sim_engine.c`):
- Instancia independiente de MMU, procesos, páginas y algoritmo.
- Corre instrucciones (`new/use/delete/kill`) y gestiona page faults.
- Lleva estadísticas propias (faults, hits, evictions, etc.).

**Algorithms** (`algorithms.c`):
- Estado privado por simulador (`AlgorithmState`).
- Hooks para notificar carga/expulsión/acceso de páginas.
- `choose_victim()` retorna el ID de la página a expulsar según la política activa.

### TODO:

- **Vamos por el paso 8 de la guia**: Implementar del 9 en adelante.
