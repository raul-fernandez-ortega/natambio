# python_pca4drc

*Disponible también en: [English](README.md)*

PCA de respuestas impulsivas para DRC, en Python.

Realiza la descomposición PCA de un conjunto de respuestas impulsivas de sala
para obtener un impulso de referencia con el que generar los filtros FIR de
corrección (DRC); el fundamento del método se desarrolla en la nota técnica
[Aplicación del PCA a medidas acústicas impulsivas de altavoces](../../docs/pca4drc/pca4drc_es.md).
Sólo se apoya en `numpy`, `scipy` y `soundfile`, **sin ningún análisis gráfico**,
por lo que es ejecutable tanto en GNU/Linux como en MS Windows.

Contenido:

- `pca4drc.py` — descomposición PCA de un directorio de impulsos (ver abajo).
- `sweepgen.py` — genera un log-sweep y su inversa (par de excitación/
  deconvolución): `python sweepgen.py sweep.xml [-s sweep.wav] [-i inverse.wav]` (ver abajo).
- `fft_convolve.py` — convolución de dos WAV por FFT (`scipy.signal.fftconvolve`).
  Útil para deconvolucionar sweeps medidos con su inversa y obtener impulsos:
  `python fft_convolve.py <wav_1> <wav_2> <wav_salida>`.
- `check_capture.py` — analiza una captura y avisa de clipping / nivel bajo / SNR
  baja: `python check_capture.py <wav> [etiqueta] [--min-level -40] [--min-snr 20]`.
- `wav2raw.py` — convierte WAV a raw (float 32-bit LE, sin cabecera), el formato
  que lee DRC: `python wav2raw.py <wav> [<wav> ...]` (crea `<nombre>.raw` por cada
  uno). Conversión exacta (no se usa ecasound porque recorta a [-1, 1]).
- `raw2wav.py` — inversa de `wav2raw.py`: convierte raw (float 32-bit LE) a WAV.
  Como el raw no lleva cabecera, la frecuencia se indica con `--rate`:
  `python raw2wav.py <raw> [<raw> ...] [--rate 48000]` (crea `<nombre>.wav`).
- `measure_pca4drc.sh` — plantilla de la cadena completa de medición (ver abajo).

## Uso

```sh
python pca4drc.py <directorio_impulsos> <output_len> [--normalize true|false]
                  [--align peak|xcorr] [--impulse-glob PATRON]
```

- `directorio_impulsos`: carpeta con las respuestas impulsivas ya medidas, en
  formato WAV.
- `output_len`: longitud (en muestras) de los WAV generados.
- `--normalize true|false` (por defecto `true`): si es `true`, las componentes se
  dividen por el pico de la componente principal (la principal queda con pico
  1.0); si es `false`, se guardan los valores PCA crudos (sin normalizar).
- `--align peak|xcorr` (por defecto `peak`): cómo se alinean las impulsivas antes
  de la PCA (ver más abajo).

Las componentes PCA se guardan en un subdirectorio `pca4drc/` **dentro** del
directorio de entrada (se crea si no existe), numeradas por su orden en el
algoritmo empezando en 0: `PCA_0.wav` (componente principal), `PCA_1.wav`, ...

## Algoritmo

1. Lee todos los `.wav` del directorio con `soundfile`.
2. Localiza el pico (máximo absoluto) de cada impulso.
3. Reescribe cada impulso en una señal de longitud `output_len` centrada en su
   pico, opcionalmente afina ese centrado por maximización de covarianza (ver
   `--align`) y le aplica una ventana de Blackman.
4. Resta la media de cada impulso, calcula la matriz de covarianza entre
   impulsos, sus autovalores/autovectores y proyecta los impulsos sobre los
   autovectores (PCA).
5. Corrige la polaridad, opcionalmente normaliza cada componente por el máximo de
   la componente principal (ver `--normalize`) y las guarda como WAV de longitud
   `output_len`.

## Alineado: `--align peak|xcorr`

`peak` (por defecto) centra cada impulsiva en su propio pico de muestra: decide
una sola muestra y el resultado queda cuantizado a la rejilla de muestreo. Media
muestra a 48 kHz son 10 µs, que a 16 kHz equivalen a 60° de fase, así que lo que
queda después del centrado no es despreciable para la covarianza.

`xcorr` parte de ese centrado y lo afina a fracción de muestra maximizando la
covarianza entre **todas las parejas** de impulsivas: los retardos por parejas se
resuelven por mínimos cuadrados con suma cero (usar todas las parejas, y no una
impulsiva de referencia, evita que una sola medida arrastre al conjunto), el pico
de correlación se refina por interpolación parabólica y el desplazamiento se
aplica con fase lineal. La ventana de correlación (±20 ms) solo tiene que cubrir
el directo y las primeras reflexiones; el tope de ±2 ms impide que una
correlación patológica lleve una impulsiva a otro arribo.

Medido sobre un sistema real de cuatro vías, 16 posiciones por vía: los retardos
residuales son de 0,26 a 0,61 muestras rms, y la varianza explicada por la
componente principal sube del 41–61 % al 50–67 %, mientras la segunda componente
baja de ~10 % a ~4 %. Es decir, buena parte de lo que parecía un segundo modo
acústico era desalineamiento sub-muestra.

Lo que `xcorr` **no** hace es acercar la componente principal al promedio en
potencia del conjunto: de una posición a otra la sala cambia por nulos modales y
patrones de reflexión, no por retardos, y ningún retardo por impulsiva convierte
una suma coherente en una media de potencia.

El informe imprime, además de los retardos aplicados, un valor de similitud media
y mínima entre parejas. Es descriptivo del conjunto —acompaña a la varianza
explicada—, no un criterio de aprobado: en medidas reales va de ~0,7 entre
posiciones próximas a ~0,4 entre lejanas, un rango en el que también puede caer
un conjunto defectuoso.

## Referencia de nivel: `--level-normalize`

La componente principal está definida salvo un factor de escala arbitrario, y ese
factor es **distinto en cada vía**: depende de la estructura de covarianza del
conjunto de impulsivas de esa vía, de la normalización por pico del paso 5 y de la
resta de media y el ventaneado de los pasos 3-4. Después DRC normaliza también sus
propias etapas (`ISNormType`, `PSNormType`, `MSNormType`), de modo que el filtro
resultante no guarda memoria de a qué nivel suena esa vía. Si mides cuatro vías y
las pasas por la cadena, sus niveles relativos dejan de significar nada: en un
sistema de cuatro vías el factor arbitrario variaba 5 dB entre ellas.

Ningún tipo de normalización de DRC lo resuelve, porque S, E, M y P son criterios
anticlipping calculados a partir del propio filtro. La referencia hay que
restituirla sobre los filtros ya terminados, y se toma **de las propias medidas**:

```sh
python pca4drc.py --level-normalize i_*/rms.wav [--ref-band LO HI] [--in-place] [--dry-run]
```

Cada filtro se escala para que **la potencia en banda que su vía entrega después de
corregir iguale la que entregaba al medir**. Ambas se calculan como promedio de
potencias sobre las posiciones de medida —la media aritmética de las potencias por
posición, que es la energía que la vía aporta a la zona de escucha—, y no como media
de las diferencias por posición en dB, que pesaría igual cada punto por poco que
contribuya. Las dos difieren en más de un decibelio sobre material con rizado modal.

Las medidas se leen del directorio del propio filtro, que es donde las deja la
cadena de medición; `--impulse-glob` (por defecto `*_impulse_*.wav`) dice cómo se
llaman.

No se supone nada sobre la forma del filtro, y eso es lo que hace el método exacto.
Normalizar el filtro contra ruido blanco —el criterio anterior— supone que es plano
dentro de la banda; un filtro de corrección es aproximadamente el inverso de la
respuesta que corrige, así que están anticorrelados ahí dentro y el error depende de
cuánto varíe la respuesta de cada vía en la banda. Sobre un sistema real de cuatro
vías ese criterio dejaba hasta **1.9 dB** de desajuste entre vías, mientras que el
actual da **0.00 dB** por construcción.

La referencia es el equilibrio que el sistema tenía **al medir**, y vale lo que
valiera aquel equilibrio. La banda de referencia tiene que caer dentro del paso de
todas las vías comparadas.

El informe imprime, por filtro, el nivel medido, el nivel que tendría corregido sin
escalar, la ganancia aplicada, cuánto varió ese nivel entre posiciones de medida y
las tres cifras que necesita el convolucionador para dejar headroom: pico de muestra,
`suma|h|` y la ganancia máxima de la respuesta de magnitud.

## Selección de las medidas: `--impulse-glob`

Tanto el PCA como `--level-normalize` toman **solo los ficheros que casan con el
patrón**, por defecto `*_impulse_*.wav`, y no todos los `.wav` del directorio. La
razón es que DRC deja sus propias salidas (`rms.wav`, `rps.wav`) en esa misma
carpeta: leyendo todo, la primera pasada sobre un directorio limpio sale bien pero
**cualquier reejecución realimenta los filtros anteriores dentro del PCA como si
fueran medidas**. Se nota en el informe, porque el número de impulsivas no cuadra y
el desalineamiento residual se dispara. Si tus medidas se llaman de otro modo, pasa
el patrón con esta opción.

## Generación del sweep: `sweepgen.py`

Genera el barrido logarítmico de excitación y su filtro inverso, el par que
luego usa la cadena de medición (`SWEEP` / `INVERSE`). Está escrito en Python
puro (numpy + soundfile):

```sh
python sweepgen.py sweep.xml [-s sweep.wav] [-i inverse.wav]
```

Los parámetros se leen de un XML `<generate_sweep>`:

```xml
<generate_sweep>
  <params>
    <sample_rate>48000</sample_rate>
    <amplitude>0.5</amplitude>
    <Hzstart>20</Hzstart>
    <Hzend>20000</Hzend>
    <length>6</length>        <!-- duración del barrido, s -->
    <silence>1</silence>      <!-- silencio al inicio y al final, s -->
    <leadin>0.05</leadin>     <!-- fracción del barrido con ventana de entrada -->
    <leadout>0.005</leadout>  <!-- fracción del barrido con ventana de salida -->
  </params>
  <sweep_filename>sweep_48k.wav</sweep_filename>
  <inverse_filename>inverse_48k.wav</inverse_filename>
</generate_sweep>
```

`-s` / `-i` sobrescriben los nombres de fichero del XML. La salida es WAV en
coma flotante de 32 bits. El barrido incluye los silencios; la inversa tiene
exactamente la
longitud del barrido (`length` segundos).

## Cadena completa de medición: `measure_pca4drc.sh`

Plantilla derivada de `ecasound_script.sh` para el sistema panambio de cuatro
altavoces (JACK + Interfaz Audio). Encadena cinco fases:

0. **Sweep** (`sweepgen.py`): genera el log-sweep de excitación (`SWEEP`) y su
   inversa (`INVERSE`) a partir del bloque de parámetros de generación
   (`SWEEP_RATE`, `SWEEP_LENGTH`, ...). `SWEEP_RATE` debe coincidir con la
   frecuencia de muestreo de captura (48000 Hz). Si ya tienes un par
   sweep/inversa, desactívala con `DO_SWEEP=0`.
1. **Medición** (`ecasound`): por cada posición de micrófono reproduce el sweep
   por cada vía y graba la respuesta. El número de vías depende de
   `FULL_NATAMBIO`: `true` (por defecto) mide las cuatro vías del NatAmbio
   completo (front L/R + rear L/R); `false` mide sólo 2 altavoces (front L/R).
   Antes de medir arranca **natambio** con la configuración correspondiente
   según `FULL_NATAMBIO` (full/half) y `SUBWOOFER` (subwoofer/normal):
   `{full,half}_natambio_measurements_{subwoofer,normal}.xml`. Su salida
   (stdout/stderr) se deriva a `NATAMBIO_LOG` (`/tmp/natambio_measure.log` por
   defecto) para no ensuciar la consola, y se comprueba que arranca y registra
   sus puertos JACK (si no, aborta mostrando el final del log). ecasound envía el
   sweep a los puertos de entrada de natambio (`natambio:front_input_left`, ...).
   Una vez en marcha, imprime un **informe de configuración** (modo full/half,
   modo subwoofer, vías a medir y el enrutado real de las salidas de natambio a
   las salidas de la tarjeta `system:playback_*`, consultado por JACK) y pide
   **confirmación** antes de medir. Al terminar la medición, natambio se detiene
   automáticamente (SIGINT, con escalada a SIGTERM/SIGKILL si no responde).
   Tras cada captura, `check_capture.py` analiza el WAV y avisa de clipping,
   nivel bajo (`MIN_LEVEL`, def. -40 dBFS) o SNR baja (`MIN_SNR`, def. 20 dB);
   si los niveles no son válidos, la medida no avanza: pide reajustar la
   ganancia del previo de micrófono y la repite (salvo en `AUTO=1`).
2. **Impulsos** (`fft_convolve.py`): deconvoluciona cada sweep grabado con el
   sweep inverso para obtener la respuesta impulsiva.
3. **PCA** (`pca4drc.py`): por cada vía genera `i_<via>/pca4drc/` con los WAV de
   las componentes PCA y, a continuación, los convierte a `.raw` (float 32-bit
   LE) con `wav2raw.py` en el mismo directorio, para alimentar a DRC.
4. **DRC** (`drc` de Sbragion): por cada vía ejecuta `drc` con `config.drc`
   (junto a este script) —o con un config distinto por par, ver
   `DRC_CONFIG_FRONT` / `DRC_CONFIG_REAR` más abajo—, sobrescribiendo
   `--BCBaseDir=i_<via>/` (la carpeta de impulsos, al mismo nivel que el
   `p_left/` original para que las rutas
   relativas del config, p.ej. la curva objetivo `../target/...`, sigan
   resolviendo) y `--BCInFile=pca4drc/PCA_0.raw` (la componente principal). Al
   terminar, convierte las salidas `rps.raw` (`PSOutFile`) y `rms.raw`
   (`MSOutFile`), generadas en `i_<via>/`, a WAV con `raw2wav.py`. Activable con
   `DO_DRC` (1 por defecto). Si una vía falla, se avisa y se continúa.

### Configuración por línea de comandos

Todas las variables tienen un valor por defecto pero pueden **sobrescribirse al
vuelo** anteponiéndolas a la llamada en la misma línea (sin editar el script):

```sh
VARIABLE=valor ./measure_pca4drc.sh
VAR1=a VAR2=b ./measure_pca4drc.sh        # varias a la vez
```

Funcionan así porque el script las lee con `${VARIABLE:-valor_por_defecto}`. Las
más habituales:

```sh
./measure_pca4drc.sh                       # las cinco fases, interactivo (4 altavoces, normal)
FULL_NATAMBIO=false ./measure_pca4drc.sh   # sistema de 2 altavoces (sólo front L/R)
SUBWOOFER=true ./measure_pca4drc.sh        # arranca natambio con la config de subwoofer
AUTO=1 ./measure_pca4drc.sh                # sin pausas interactivas
DO_SWEEP=0 ./measure_pca4drc.sh            # usar un sweep/inversa ya existentes
DO_MEASURE=0 ./measure_pca4drc.sh          # saltar la medición (re-procesar lo ya medido)
DO_DRC=0 ./measure_pca4drc.sh              # todo menos la corrección DRC
DO_SWEEP=0 DO_MEASURE=0 DO_IMPULSES=0 ./measure_pca4drc.sh  # sólo PCA + DRC
DO_MEASURE=0 DO_IMPULSES=0 DO_PCA=0 ./measure_pca4drc.sh    # sólo DRC sobre PCA_0.raw ya generados
FULL_NATAMBIO=false NUM_POS=8 ./measure_pca4drc.sh         # 2 altavoces, 8 posiciones
OUTPUT_LEN=65536 PCA_NORMALIZE=false ./measure_pca4drc.sh  # ajustar parámetros de PCA
IN_MEAS=system:capture_2 ./measure_pca4drc.sh             # micrófono en otra toma de captura
SELECT_INPUT=1 ./measure_pca4drc.sh                        # elegir la toma de micrófono por menú
```

Los cinco interruptores de fase `DO_SWEEP` / `DO_MEASURE` / `DO_IMPULSES` /
`DO_PCA` / `DO_DRC` valen `1` (activada) o `0` (saltada) y son independientes,
así que pueden combinarse para ejecutar sólo las fases que interesen.

### Puertos JACK de salida (sweep) y de entrada (micrófono)

Cada medida la realiza `ecasound` con dos cadenas: una **reproduce** el sweep
hacia la entrada de natambio y otra **graba** la captura del micrófono.

- **Salida (sweep → natambio):** el destino lo fija el array `OUT_PORTS`, una
  entrada por vía, apuntando a los puertos de entrada de natambio
  (`natambio:front_input_left`, `natambio:front_input_right`, ...). No suele
  hacer falta tocarlo: natambio fija esos nombres.
- **Entrada (micrófono → WAV):** es un único puerto común a todas las vías, la
  variable `IN_MEAS` (por defecto `system:capture_1`, la primera toma de captura
  de la tarjeta, donde suele estar el previo de micrófono).

ecasound se conecta a esos puertos con `jack_auto` (autoconexión): la salida a
`OUT_PORTS[$w]` y la entrada a `IN_MEAS`.

Hay dos formas de indicar la toma de micrófono:

```sh
IN_MEAS=system:capture_2 ./measure_pca4drc.sh   # fijarla directamente
SELECT_INPUT=1 ./measure_pca4drc.sh             # elegirla por menú interactivo
```

Con `SELECT_INPUT=1`, tras arrancar natambio el script lista los puertos JACK de
captura disponibles (`jack_lsp -o`, p.ej. `system:capture_*`) y permite asignar
uno como `IN_MEAS` por número (0 = mantener el actual). Sólo actúa en modo
interactivo (se ignora con `AUTO=1`) y si `jack_lsp` está disponible; funciona
también en el modo calibración (`CALIBRATE=1`).

Lista completa de variables configurables por entorno: `FULL_NATAMBIO`
(true = 4 altavoces, false = 2) y `SUBWOOFER` (true = config con subwoofer,
false = normal); arranque de natambio `NATAMBIO_BIN`, `NATAMBIO_CONFIG`
(por defecto el XML elegido según `FULL_NATAMBIO`/`SUBWOOFER`), `NATAMBIO_LOG`;
generación del sweep (Fase 0) `SWEEP_RATE`, `SWEEP_AMPLITUDE`, `SWEEP_HZSTART`,
`SWEEP_HZEND`, `SWEEP_LENGTH`, `SWEEP_SILENCE`, `SWEEP_LEADIN`, `SWEEP_LEADOUT`;
medición `NUM_POS`, `SWEEP`, `INVERSE`, `IN_MEAS`, `SELECT_INPUT`, `GAIN_OUT`,
`GAIN_IN`, `REC_SECONDS`, `MIN_LEVEL`, `MIN_SNR`; PCA `OUTPUT_LEN`, `PCA_NORMALIZE`; DRC
`DRC_BIN`, `DRC_CONFIG` (por defecto `config.drc` junto al script),
`DRC_CONFIG_FRONT` y `DRC_CONFIG_REAR` (configuraciones por par, cada una con
`DRC_CONFIG` como valor de respaldo, para corregir hacia objetivos distintos el
par frontal —normalmente con subwoofer— y el trasero), `DRC_PS_OUT`
(rps.raw), `DRC_MS_OUT` (rms.raw); y los interruptores de fase `DO_SWEEP` /
`DO_MEASURE` / `DO_IMPULSES` / `DO_PCA` / `DO_DRC` / `AUTO`. Si los scripts `.py`
no están junto al `.sh`, exporta `TOOLS_DIR` apuntando a ellos.

## Dependencias

```sh
pip install -r requirements.txt
```
