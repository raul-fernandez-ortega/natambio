# python_nae_natambio

*Disponible también en: [English](README.md)*

Versión **Python offline** del algoritmo **NAE** (*NatAmbio Ambient Extraction*).

Es el mismo algoritmo que las otras dos encarnaciones del proyecto, pero fuera de
tiempo real:

| Implementación | Lenguaje | Contexto |
| --- | --- | --- |
| `tools/ladspa_nae_natambio` | C | Plugin LADSPA (tiempo real) |
| `src/nae.cpp` | C++ | Motor NAE del cliente JACK `natambio` (tiempo real) |
| **`tools/python_nae_natambio`** | **Python** | **Offline sobre un fichero WAV** |

Este directorio es un **proyecto Python autónomo**: no comparte código con el
resto del repositorio.

## Qué hace

Descompone una señal estéreo mediante PCA (autovalores/autovectores de la matriz
de covarianza 2×2 sobre las componentes *mid/side*, con una ventana solapada de
`covsteps` frames) en dos componentes:

- **C1 — principal (main)**
- **C2 — ambiente (ambient)**

y las escribe como `<entrada>_c1.wav` y `<entrada>_c2.wav` junto al WAV de
entrada.

Tiene dos propósitos:

1. **Ejecutar** el algoritmo NAE sobre un WAV de forma reproducible, sin
   necesidad de un servidor de audio en tiempo real (JACK).
2. **Analizar** el proceso: con `--analysis true` genera gráficas matplotlib
   (correlación L/R y su histograma, rotación de autovectores, relación de
   autovalores, niveles de las componentes C1/C2, y dispersión *mid/side* con los
   autovectores superpuestos). Cada título incluye el nombre del WAV y el modo.

## Multiplataforma

Se apoya únicamente en `numpy`, `soundfile` y `matplotlib` —todas
multiplataforma—, por lo que es ejecutable tanto en **GNU/Linux** como en
**MS Windows** sin cambios.

## Dependencias

- Python 3
- [`numpy`](https://numpy.org/)
- [`soundfile`](https://python-soundfile.readthedocs.io/) (lectura/escritura WAV)
- [`matplotlib`](https://matplotlib.org/) (gráficas de análisis)

### Instalación

Se recomienda un entorno virtual.

**GNU/Linux**

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install numpy soundfile matplotlib
```

**MS Windows** (PowerShell)

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install numpy soundfile matplotlib
```

> En Windows, `soundfile` incluye la librería `libsndfile`; en algunas distros
> GNU/Linux puede hacer falta instalar el paquete del sistema `libsndfile1`.

## Uso

```sh
python nae_natambio.py <fichero.wav> [--mode alpha|beta]
                                     [--analysis true|false]
                                     [--frame-size N] [--covsteps N]
```

### Argumentos

| Argumento | Valor por defecto | Descripción |
| --- | --- | --- |
| `wavfile` | *(obligatorio)* | Fichero WAV **estéreo** a analizar. |
| `--mode` | `alpha` | Modo NAE: `alpha` (principal/main) o `beta` (ambiente/ambient). |
| `--analysis` | `true` | `true` = genera las gráficas matplotlib; `false` = solo procesa y escribe los WAV. |
| `--frame-size` | `1024` | Tamaño de frame en muestras. |
| `--covsteps` | `5` | Número de pasos de covarianza solapados. |

El flag `--analysis` acepta `true/false`, `1/0`, `yes/no`, `on/off`.

### Ejemplos

Procesar en modo alpha (principal) con análisis (abre las gráficas):

```sh
python nae_natambio.py entrada.wav --mode alpha --analysis true
```

Procesar en modo beta (ambiente) sin gráficas (solo genera los WAV de salida):

```sh
python nae_natambio.py entrada.wav --mode beta --analysis false
```

## Salida

Junto al fichero de entrada se generan:

- `<entrada>_c1.wav` — componente principal (C1)
- `<entrada>_c2.wav` — componente ambiente (C2)

En modo análisis, además, se muestran las ventanas de matplotlib (cada una
bloquea hasta que se cierra).
