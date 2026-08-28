# python_xtc_filters

Generadores en Python puro de filtros FIR **XTC** (cancelación de diafonía), en
dos variantes:

| Script | Disposición | Filtros que escribe |
|---|---|---|
| `xtc_filters.py` | simétrica | directo + cruzado |
| `xtc_filters_asym.py` | asimétrica | directo (compartido) + cruzado izquierdo + cruzado derecho |

Ambos escriben WAV float de 32 bits en `./filters/`.

Son las contrapartidas en Python de las herramientas en C de
[`tools/xtc_filters`](../xtc_filters), que enlazan el código de diseño de filtros
compartido en `../../lib` (`xtc.c` / `xtc_asym.c` → `dsp.c` →
`binaural_cues.c`). Los scripts reimplementan esa misma tubería en NumPy/SciPy
para poder ejecutarse sin compilar la cadena de herramientas C — útil para
experimentar, enseñar y contrastar la salida de la versión C.

Son un port fiel de las **herramientas en C**, no del antiguo
`~/ambio_filters/ambio_filters_scipy.py`. La única diferencia sustancial con ese
script antiguo está en el paso de fase mínima: aquí (igual que en `lib/dsp.c`) el
cepstrum homomórfico se calcula sobre una rejilla sobremuestreada ×8, lo que
mantiene el error de magnitud por debajo de ~0.0002 dB. El script original
transformaba a longitud `n`, aliasando la cola del cepstrum (deriva ~0.2 dB,
amplificada a ~5 % a través de las 16 convoluciones encadenadas del XTC). Los
filtros de salida son por tanto equivalentes a los de la versión C, y usan el
mismo contrato de nombre de archivo.

`xtc_filters_asym.py` importa las primitivas de DSP (modelo ILD, fase mínima,
constantes) de `xtc_filters.py` en lugar de copiarlas. La versión C sí las
duplica en `xtc_asym.c`, pero únicamente porque `lib/xtc.c` está replicado por
ports de terceros y tiene que permanecer estable byte a byte; aquí no existe esa
restricción.

## Plataforma

Los scripts son multiplataforma: dependen sólo de `numpy`, `scipy` y `soundfile`
(todos con wheels para Windows, macOS y Linux) más `tomllib` de la biblioteca
estándar, y no usan ninguna llamada específica de POSIX, así que se ejecutan sin
cambios en **GNU/Linux y Microsoft Windows** (y macOS). En Windows se invocan
directamente con `python xtc_filters.py ...` — el paso `make install` de
autotools y los lanzadores `natambio-xtc-filters-py` son sólo de Unix, pero son
comodidades de empaquetado, no requisitos.

## Requisitos

```sh
pip install -r requirements.txt   # numpy, scipy, soundfile
```

La lectura de TOML usa `tomllib` de la biblioteca estándar en **Python 3.11 o
posterior**, de modo que ahí no hace falta nada más. En intérpretes anteriores
`requirements.txt` instala `tomli`, al que los scripts recurren automáticamente.

## Ficheros de configuración

Los parámetros vienen de un fichero TOML, que se pasa con `-c`. El formato es
exactamente el de las herramientas en C, y los ejemplos comentados viven junto a
ellas en lugar de duplicarse aquí, para que las dos versiones no puedan
divergir:

| Fichero | Qué muestra |
|---|---|
| [`../xtc_filters/xtc_sym_default.toml`](../xtc_filters/xtc_sym_default.toml) | simétrico, reproduciendo los valores por defecto |
| [`../xtc_filters/xtc_sym_wide.toml`](../xtc_filters/xtc_sym_wide.toml) | simétrico, ajustado para una imagen más amplia |
| [`../xtc_filters/xtc_asym_geometry.toml`](../xtc_filters/xtc_asym_geometry.toml) | colocación asimétrica de los altavoces |
| [`../xtc_filters/xtc_asym_room.toml`](../xtc_filters/xtc_asym_room.toml) | colocación simétrica en una sala acústicamente asimétrica |

Instalados desde los paquetes Debian, quedan en
`/usr/share/doc/natambio-drc/examples/`.

```toml
sample_rate = 48000
filter_len  = 4096
frac_delay  = true    # opcional; ITD exacto en vez de redondeado (ver abajo)
model_delay = 64      # opcional; retardo global del camino fraccionario

[xtc]                 # [left] y [right] en el script asimétrico
itd_us      = 170
ild_db      = 14.0
ild_alpha   = 2.0
azimuth_deg = 20

[output]
directory = "filters"
prefix    = "mi_sala" # opcional
```

Igual que en las herramientas C, una clave desconocida es un error y no una línea
ignorada en silencio, así que una errata como `ild_alfa` falla en lugar de dejar
`ild_alpha` en su valor por defecto.

## Uso

Simétrico, por TOML, por flags, o ambos:

```sh
python3 xtc_filters.py -c ../xtc_filters/xtc_sym_default.toml
python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha -z azimut_grados -r frec_muestreo -f longitud
python3 xtc_filters.py -c ../xtc_filters/xtc_sym_default.toml -l 12   # el fichero, con un valor cambiado
# por defecto: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096 -M 64
# -F : ITD fraccionario (ver abajo);  -M N : su retardo de modelado
# -d : vuelca además los filtros intermedios ILD_<az>_deg.wav y MP_ILD_<az>_deg.wav
```

`-c` se lee en el punto de la línea de comandos donde aparece, de modo que los
flags colocados **después** sobrescriben el fichero y los colocados antes no.

Asimétrico, solo por TOML, por la misma razón que en la herramienta C: ocho
números en una línea de comandos, la mitad de ellos distinguiéndose de la otra
mitad por una sola letra, es exactamente la forma de error que produce un filtro
verosímil para la geometría equivocada.

```sh
python3 xtc_filters_asym.py -c ../xtc_filters/xtc_asym_geometry.toml
```

Instalados mediante el build de autotools (`make install`), están también
disponibles como los lanzadores `natambio-xtc-filters-py` y
`natambio-xtc-filters-asym-py`.

## ITD fraccionario

Desactivado por defecto. La recursión XTC coloca sus taps en muestras enteras,
así que `itd_us` se redondea primero: a 48 kHz, 170 µs son 8,16 muestras y pasan
a 8. Ese redondeo no sale gratis. Un error de ITD `dt` deja un residuo
`2·sin(π·f·dt)` relativo a la señal cancelante, de modo que la cancelación que
el propio diseño pretende queda limitada:

| Error de ITD | tope a 10 kHz, 48 kHz |
|---|---|
| 0,5 muestras | −3,8 dB |
| 0,16 muestras (los 170 µs por defecto) | −13,6 dB |
| 0,024 muestras | −30 dB |

Con `frac_delay = true` (o `-F` en la herramienta simétrica) la misma recursión
se evalúa en frecuencia, donde un tap es un factor de fase lineal `exp(-j2πfτ)`
—el operador de retardo limitado en banda exacto— y el ITD no necesita
redondeo. Nada más cambia en el pipeline: el modelo ILD, el paso a fase mínima y
la escalera son los mismos, peldaño a peldaño. Cuando el ITD resulta ser un
número entero de muestras los dos caminos coinciden a precisión numérica, que es
lo que verifica `make check` en `../../lib`.

`model_delay` (`-M`, por defecto 64) es el retardo global que el camino
fraccionario añade a todos los filtros, para que la respuesta impulsional de dos
lados de un desplazamiento fraccionario no se recorte en n = 0. Es común a todos
los filtros de un diseño, y el XTC solo depende de los retardos *entre* ellos,
así que cuesta latencia y nada más: 1,3 ms a 48 kHz, con ~20 dB de margen sobre
el suelo de la propia recursión.

Medido contra la planta modelada, la ganancia sobre el diseño redondeado va de
42 dB en medios-graves a 58 dB por encima de 8 kHz; los números y el script de
medida están en
[`compare_frac_delay.py`](compare_frac_delay.py).
Son cifras de auto-consistencia —cuánta de la intención del diseño sobrevive a
la implementación—, no una afirmación sobre una sala real, donde el desajuste de
HRTF y el movimiento de cabeza mandan. Lo que cambia es que el redondeo del ITD
deja de ser uno de los límites.

La herramienta asimétrica gana más: redondea el `itd_us` de cada lado por
separado y el periodo de ida y vuelta `itd_left + itd_right` hereda ambos
errores, con lo que una geometría puede arrastrar hasta una muestra entera de
error de periodo.

Los nombres de salida ganan el sufijo `_frac` (simétrico sin prefijo), o el
prefijo asimétrico por defecto pasa a ser `XTC_asym_frac`, de modo que los dos
diseños no puedan pisarse.

## Salida

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav   # simétrico, sin prefijo
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
<prefijo>_direct.wav                                             # con prefijo
<prefijo>_cross.wav                                              # simétrico
<prefijo>_cross_left.wav                                         # asimétrico
<prefijo>_cross_right.wav                                        # asimétrico
```

El script asimétrico escribe **tres** filtros, no cuatro: los dos filtros
directos del modelo son idénticos, ya que dependen únicamente del operador de
ida y vuelta *P = G_l ∗ G_r*, que es simétrico bajo intercambio de canales.

Atención a qué lado alimenta a qué altavoz. `<prefijo>_cross_left.wav` alimenta
el altavoz **izquierdo** pero se construye con los parámetros del lado
**derecho**, porque cancela la fuga del altavoz derecho hacia el oído izquierdo y
el único camino directo que llega a ese oído es el del altavoz izquierdo.

## Tubería

1. **Curva ILD objetivo** (`ild_db_model`): `−ild_log_empirical` por encima de
   200 Hz, extrapolación de −6 dB/oct por debajo, más un shelf HF de −36 dB/oct
   por encima de 20 kHz.
2. **FIR de fase lineal** con `scipy.signal.firwin2` (ventana Hamming) sobre la
   rejilla densa `1 + 2^ceil(log2(filter_len))`, normalizado en RMS.
3. **Fase mínima** con el cepstrum homomórfico sobremuestreado ×8, normalizado
   en RMS.
4. **Normalización L2** y la **recursión XTC**: 32 pasos alternos directo/cruzado
   en el caso simétrico, 16 escalones de ida y vuelta en el asimétrico —el mismo
   número de términos—, cada uno convolucionado con el filtro ILD de fase mínima
   y truncado a `filter_len`.

En el caso asimétrico el paso 3 se ejecuta tres veces: un filtro por lado, más el
filtro de ida y vuelta, cuya log-magnitud es la media de las de ambos lados. Como
el modelo de ILD es lineal en `alpha·sin(theta)`, esa media se obtiene evaluando
el mismo modelo en `theta = pi/2` con la media de los dos productos, sin
promediar respuestas. Con ambos lados iguales colapsa en el filtro de un lado,
que es lo que hace que el generador asimétrico se reduzca al simétrico.

## El balance, en el caso asimétrico

Estos coeficientes son **M⁻¹**: el ajuste de nivel entre canales no se hornea en
ellos deliberadamente. Hay que aplicarlo después, como ganancia sobre las dos
convoluciones que alimentan la misma salida, atenuando el canal que llega más
fuerte y sin amplificar nunca el otro. Sin ajustar, la cancelación queda acotada
en torno a 20·log₁₀|1−b|: un error de 1 dB sitúa el techo cerca de −19 dB. El
procedimiento de escucha está en el apartado del balance de
[`docs/xtc/xtc_no_simetrico_es.md`](../../docs/xtc/xtc_no_simetrico_es.md).

## Relacionado

- Los mismos filtros los puede generar `natambio` en proceso mediante bloques
  `<xtc>` y `<xtc_asym>` (ver `docs/README.CONFIG`); estos scripts los producen
  offline como ficheros WAV.
- Las herramientas en C: [`../xtc_filters`](../xtc_filters).
- El modelo: [`docs/xtc/xtc_filters_es.md`](../../docs/xtc/xtc_filters_es.md)
  (simétrico) y
  [`docs/xtc/xtc_no_simetrico_es.md`](../../docs/xtc/xtc_no_simetrico_es.md)
  (asimétrico).
