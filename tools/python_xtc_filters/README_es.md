# python_xtc_filters

Generador en Python puro de filtros FIR **XTC** (cancelación de crosstalk).
Escribe el par de filtros *direct* y *cross* como WAV float de 32 bits en
`./filters/`.

Es la contrapartida en Python de la herramienta en C
[`tools/xtc_filters`](../xtc_filters), que enlaza el código de diseño de filtros
compartido en `../../lib` (`xtc.c` → `dsp.c` → `binaural_cues.c`). Este script
reimplementa esa misma tubería en NumPy/SciPy para poder ejecutarse sin compilar
la cadena de herramientas C — útil para experimentar, enseñar y contrastar la
salida de la versión C.

Es un port fiel de la **herramienta en C**, no del antiguo
`~/ambio_filters/ambio_filters_scipy.py`. La única diferencia sustancial con ese
script antiguo está en el paso de fase mínima: aquí (igual que en `lib/dsp.c`) el
cepstrum homomórfico se calcula sobre una rejilla sobremuestreada ×8, lo que
mantiene el error de magnitud por debajo de ~0.0002 dB. El script original
transformaba a longitud `n`, aliasando la cola del cepstrum (deriva ~0.2 dB,
amplificada a ~5 % a través de las 16 convoluciones encadenadas del XTC). Los
filtros de salida son por tanto equivalentes a los de la versión C, y usan el
mismo contrato de nombre de archivo.

## Plataforma

El script es multiplataforma: depende sólo de `numpy`, `scipy` y `soundfile`
(todos con wheels para Windows, macOS y Linux) y no usa ninguna llamada
específica de POSIX, así que se ejecuta sin cambios en **GNU/Linux y Microsoft
Windows** (y macOS). En Windows se invoca directamente con
`python xtc_filters.py ...` — el paso `make install` de autotools y el lanzador
`natambio-xtc-filters-py` son sólo de Unix, pero son comodidades de empaquetado,
no requisitos.

## Requisitos

```sh
pip install -r requirements.txt   # numpy, scipy, soundfile
```

## Uso

```sh
python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha -z azimut_grados -r sample_rate -f filter_len
# por defecto: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096
# -d : vuelca además los filtros intermedios filters/ILD_<az>_deg.wav y MP_ILD_<az>_deg.wav
```

Instalado mediante el build de autotools (`make install`), está también
disponible como el lanzador `natambio-xtc-filters-py`.

Salida (en `./filters/`):

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
```

## Tubería

1. **Curva ILD objetivo** (`ild_db_model`): `−ild_log_empirical` por encima de
   200 Hz, extrapolación de −6 dB/oct por debajo, más un shelf HF de −36 dB/oct
   por encima de 20 kHz.
2. **FIR de fase lineal** con `scipy.signal.firwin2` (ventana Hamming) sobre la
   rejilla densa `1 + 2^ceil(log2(filter_len))`, normalizado en RMS.
3. **Fase mínima** con el cepstrum homomórfico sobremuestreado ×8, normalizado
   en RMS.
4. **Normalización L2** y la **recursión XTC** (`get_xtc`): 32 pasos alternos
   direct/cross, cada uno convolucionado con el filtro ILD de fase mínima y
   truncado a `filter_len`.

Los mismos filtros pueden generarse dentro del proceso de `natambio` mediante un
bloque `<xtc>` (ver `docs/README.CONFIG`); esta herramienta los produce offline
como archivos WAV.
