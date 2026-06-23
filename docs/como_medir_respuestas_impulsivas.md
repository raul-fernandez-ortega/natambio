# Cómo medir respuestas impulsivas para DRC con NatAmbio

Para aplicar ecualización por convolución de filtros FIR, lo que se conoce como Digital Room Correction (DRC), se requiere de unas medidas impulsivas de la respuesta de la sala a cada altavoz a ecualizar. Esto es independiente del convolver finalmente aplicado, sea NatAmbio o cualquier otro, y del proceso de obtención del filtro FIR DRC (en mi caso siempre uso [DRC-FIR](http://drc-fir.sourceforge.net)).

Por otro lado, junto con NatAmbio, se presenta una [propuesta de medida de impulsivas de sala basada en tomar númerosas tomas en diferentes puntos de una zona de escucha y, mediante aplicación de PCA](pca4drc/pca4drc_es.md), caracterizar las medidas a una única impulsiva que será el objetivo a invertir.

Esta guía explica, de modo básico, cómo medir las respuestas impulsivas de un sistema NatAmbio en
la sala de escucha y, a partir de ellas, obtener por PCA un impulso de referencia
con el que generar los filtros FIR de corrección de sala (DRC). Esta guia va a analizar el caso de las medidas simples tradicionales, y también el caso más avanzado de medida multipunto y caracterización mediante PCA4DRC. Incluyendo el caso de medidas de un solo dipolo y dos dipolos, así como la incorporación de un subwoofer.

Para todas las opciones, se presente una automatización con el script [`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh),
que encadena las herramientas del toolkit
[`tools/python_pca4drc/`](../tools/python_pca4drc/README.md).

Como ya se ha comentado, el fundamento del método (medición multipunto + PCA) se desarrolla en el artículo
[Aplicación del PCA a medidas acústicas impulsivas de altavoces](pca4drc/pca4drc_es.md).
Por otro lado, en la automatización propuesta, la generación de los filtros DRC propiamente dichos la realiza
[DRC-FIR](https://drc-fir.sourceforge.net/) de Denis Sbragion (programa externo).

## Concepto de medida multipunto

En lugar de medir la respuesta impulsiva en un único punto de escucha —cuya
representatividad podria ser discutible—, se toman **varias medidas** repartidas
por la región de escucha (por defecto en la automatización se definen **16 posiciones**) y se aplica un
**Análisis de Componentes Principales (PCA)** al conjunto. La **componente
principal** (`PCA_0`) condensa la información acústica común a toda la región y
atenúa los fenómenos menos correlados (p. ej. reflexiones cuya contribución cambia significativamente de una posición de medida a otra). Esa componente principal es la que
se usa como impulso de referencia para generar el filtro DRC.

## Micrófonos de medida

Para medir correctamente las mencionadas impulsivas se requiere de un micrófono omnidireccional. Existen dos tipos de micrófono que se pueden usar:

- Los clásicos, que necesitan conectarse a un previo de micrófono que lo alimente a 48 V y ajuste correctamente la ganancia a los niveles habituales para este tipo de micrófonos. Por ejemplo, el mío es un muy básico [Behringer ECM 8000](https://www.behringer.com/en/products/0506-AAA). En este campo la gama es amplia y los precios y calidades son muy variados.
- Micrófonos que ya llevan la funcionalidad de previo integrada y se conectan directamente por USB al ordenador de medición. Es muy popular el modelo [Umik-2 de MiniDSP](https://www.minidsp.com/products/acoustic-measurement/umik-2)

Si se quiere medir con alta precisión es imprescindible que se proporcione la curva individual de calibración junto con el micrófono. Con esta tabla o gráfica, se puede corregir la medida obtenida para obtener valores con menos error.

Los micrófonos omnidireccionales clásicos requieren de un previo que suele formar parte de los interfaces audio profesional [como los que se recomiendan para NatAmbio](hw_setup_es.md). Por lo tanto, el propio interfaz audio ya presenta la capacidad de medir junto con el micrófono que se conecte. Estos interfaces tienen interruptor HW o SW para alimentación phamtom y controles físicos de ganancia en entrada, y su conexión siempre es XLR.

## Qué es un logsweep

Los logsweeps son señales de medida ampliamente utilizadas en este tipo de procesos de obtención de impulsivas. Permiten obtener impulsos característicos de entornos acústicos con una excelente SNR. 
Un logsweep es un barrido senoidal cuya frecuencia aumenta progresivamente de forma logarítmica entre dos límites definidos. Esta distribución temporal hace que la energía quede repartida de forma especialmente adecuada para medidas acústicas de gran rango dinámico. Es una señal ciertamente no muy agradable de escuchar, pero más suave que el ruido blanco.

Lo que los hace muy interesantes es que los logsweep tienen una "señal hermana" que recorre las frecuencias en modo inverso y con energía en proporción inversa. Y convolucionadas una con otra el resultado es una impulsiva perfecta de fase lineal acotada al rango de medida definido.

Es la convolución de esta citada inversa con las medidas realizadas la que resulta en las impulsivas buscadas. Por lo tanto, medir con el log-sweep es el primer paso imprescindible, pero para obtener finalmente la impulsiva además hay que hacer la convolución de la medida con el filtro inverso.

## Antes de medir
 
> **Aviso importante**: en todo momento durante el proceso de medida hay que cuidar que los niveles de reproducción de las señales de barrido tonal estén controlados para evitar accidentes. Para ello es muy conveniente hacer una calibración previa: si el sistema tiene controles de volumen globales, o por dipolo, comenzar con un nivel bajo e ir subiéndolo hasta el punto en que se alcanza un nivel correcto. Esto se consigue conjugando los niveles hardware de reproducción y grabación con los niveles software del programa de medida.

Antes de medir hay que preparar todo el entorno físico y software:

- Localizar en GNU/Linux el interfaz audio a emplear y arrancar con el jackd.
- Preparar el micrófono, colocado en su pie de micrófono y conectado por XLR a la toma del previo de microfónica de la interfaz audio de NatAmbio.
- Arrancar jackd aplicada a la tarjeta de sonido.
- Identificar en jackd los nombres de la entrada desde micrófono y las salidas a cada altavoz.

A continuación se muestra un ejemplo para la interfaz audio Focusrite Scarlett 6i6:

![Focusrite Scarlett 6i6](figs/focusrite_scarlett_v01.svg)

Habitualmente, las entradas de micrófono de interfaces externos audio se corresponden con las primeras en la lista de elementos "capture" de jackd.

### Localizar la interfaz audio

Si la interfaz es USB, que es lo más habitual, es fácil localizarla con:

```
$cat /proc/asound/cards
 0 [PCH            ]: HDA-Intel - HDA Intel PCH
                      HDA Intel PCH at 0x6001120000 irq 144
 2 [USB            ]: USB-Audio - Scarlett 6i6 USB
                      Focusrite Scarlett 6i6 USB at usb-0000:00:14.0-5.4, high speed
```

```
$aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: PCH [HDA Intel PCH], device 0: ALC3266 Analog [ALC3266 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 3: HDMI 0 [HDMI 0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 7: HDMI 1 [HDMI 1]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 8: HDMI 2 [HDMI 2]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: USB [Scarlett 6i6 USB], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0

```

Identificada la interfaz, se puede arrancar jackd de la siguiente manera:

``` 
/usr/bin/jackd -R -P70 -dalsa -dhw:USB -r<SAMPLERATE> -p<BUFFERSIZE> -n3
```

Otra posibilidad es emplear la aplicación gráfica **[qjackctl](https://qjackctl.sourceforge.io/)**.

Los puertos I/O del interfaz se presentan habitualmente en jackd con los siguientes nombres:

![Jackd Focusrite Scarlett 6i6](./figs/focusrite_scarlett_natambio.png)

En el ejemplo de Focusrite Scarlett 6i6, las entradas para micrófono corresponden con ``system:capture_1`` y ``system:capture_2``.

### NatAmbio en bypass y en modo subwoofer

Una forma de estandarizar las medidas es disponiendo siempre de una sesión NatAmbio en ejecución. De esta forma no se lanza el log-sweep directamente sobre una salida del interfaz audio, sino sobre una entrada de NatAmbio. Para el caso de gestión de subwoofer desde NatAmbio, este paso es imprescindible, dado que la medida debe hacerse con el filtrado paso bajo y paso alto activo y enviado hacia monitores y subwoofer respectivamente.

Dentro de la carpeta de herramientas de medidas pca4drc, hay una serie de ficheros de configuración XML para tomar medidas con NatAmbio en bypass/gestión subwoofer. 

Hay **cuatro** ficheros de configuración, uno por cada sistema a medir, todos en
[`tools/python_pca4drc/`](../tools/python_pca4drc/). El nombre sigue el patrón
`{half,full}_natambio_measurements_{normal,subwoofer}.xml`: `half` = un dipolo
(sólo front), `full` = dos dipolos (front + rear); `normal` = sin subwoofer,
`subwoofer` = con subwoofer. El script elige el XML automáticamente según las
variables `FULL_NATAMBIO` y `SUBWOOFER`:

| Sistema a medir | Fichero XML (en `tools/python_pca4drc/`) | Variables de `measure_pca4drc.sh` | Salidas de natambio a conectar a la tarjeta |
|---|---|---|---|
| Un dipolo | `half_natambio_measurements_normal.xml` | `FULL_NATAMBIO=false` `SUBWOOFER=false` | `front_output_left`, `front_output_right` |
| Un dipolo con subwoofer | `half_natambio_measurements_subwoofer.xml` | `FULL_NATAMBIO=false` `SUBWOOFER=true` | `high_pass_front_output_{left,right}` (altavoces front) + `low_pass_front_output_{left,right}` (subwoofer) |
| Dos dipolos | `full_natambio_measurements_normal.xml` | `FULL_NATAMBIO=true` `SUBWOOFER=false` | `front_output_{left,right}` + `rear_output_{left,right}` |
| Dos dipolos con subwoofer | `full_natambio_measurements_subwoofer.xml` | `FULL_NATAMBIO=true` `SUBWOOFER=true` | `high_pass_front_output_{left,right}` + `low_pass_front_output_{left,right}` + `rear_output_{left,right}` |

> En las variantes **con subwoofer**, el dipolo *front* se reparte en una salida
> **paso-alto** (a los altavoces front) y otra **paso-bajo** (al subwoofer); el
> dipolo *rear* (en `full`) no se divide. 

En cualquiera de los cuatro ficheros, hay que hacer un ajuste para enviar las señales de salida de NatAmbio a las salidas correctas del interfaz audio:
asignar el `<destname>` de cada salida (`<jack_output>`) al puerto físico
`system:playback_*` de la tarjeta al que está conectado ese altavoz. Por ejemplo,
para `half_natambio_measurements_normal.xml` (un dipolo):

```
    <jack_output> 
      <port>
	<name>front_output_left</name>
	<destname>system:playback_1</destname>
      </port>
      <port>
	<name>front_output_right</name>
	<destname>system:playback_2</destname>
      </port>   
    </jack_output>

```
Se trata de asignar los <destname> de los canales <front_output_left> y <front_output_right> para que los barridos tonales se escuchen por el altavoz que corresponda.

La ejecución de natambio es directa, con jackd ya activo:

```
natambio [--quiet] <config.xml>
```
El parametro --quiet desactiva los mensajes de salida.

### Generar el sweep de medida

La señal de excitación es un **barrido senoidal logarítmico** (log-sweep). Para
deconvolucionar luego la grabación y obtener la respuesta impulsiva se necesitan
**dos ficheros**: el propio sweep y su **filtro inverso**. Ambos los genera de una
vez la herramienta [`sweepgen.py`](../tools/python_pca4drc/sweepgen.py) (es la
Fase 0 del flujo automatizado, y `measure_pca4drc.sh` la ejecuta sola salvo que se
salte con `DO_SWEEP=0`).

`sweepgen.py` toma los parámetros de un XML `<generate_sweep>`. Crea un fichero,
por ejemplo `sweep.xml`, con el contenido:

```xml
<generate_sweep>
  <params>
    <sample_rate>48000</sample_rate>   <!-- DEBE coincidir con la captura JACK -->
    <amplitude>0.5</amplitude>         <!-- amplitud de pico del sweep -->
    <Hzstart>20</Hzstart>              <!-- frecuencia inicial -->
    <Hzend>20000</Hzend>               <!-- frecuencia final -->
    <length>6</length>                 <!-- duración del barrido, s -->
    <silence>1</silence>               <!-- silencio al inicio y al final, s -->
    <leadin>0.05</leadin>              <!-- fracción del barrido con ventana de entrada -->
    <leadout>0.005</leadout>           <!-- fracción del barrido con ventana de salida -->
  </params>
  <sweep_filename>sweep_48k.wav</sweep_filename>
  <inverse_filename>inverse_48k.wav</inverse_filename>
</generate_sweep>
```

Y se ejecuta con:

```sh
python sweepgen.py sweep.xml
```

Esto genera `sweep_48k.wav` (el barrido de excitación, con sus silencios) e
`inverse_48k.wav` (el filtro inverso, de la longitud exacta del barrido). Los
nombres de fichero del XML se pueden sobrescribir desde la línea de comandos con
`-s` (sweep) e `-i` (inversa):

```sh
python sweepgen.py sweep.xml -s sweep_48k.wav -i inverse_48k.wav
```

> **Importante**: `sample_rate` (48000 Hz en el ejemplo) tiene que coincidir con
> la frecuencia de muestreo a la que se va a grabar por JACK. El sweep así
> generado (`sweep_48k.wav`) es justo el que se reproduce en el ejemplo de
> `ecasound` de la siguiente sección, y la inversa (`inverse_48k.wav`) la que se
> usa después para deconvolucionar.

### Tomando las medidas manualmente

Para tomar las medidas manualmente en GNU/Linux un comando muy útil y flexible es
`ecasound`. 

La idea es lanzar a la vez dos cadenas de audio dentro de la misma ejecución
de `ecasound`:

- Una cadena de reproducción que envía el sweep (`sweep_48k.wav`) a la entrada
  de natambio de la vía a medir.
- Una cadena de grabación que captura el micrófono (`system:capture_1`) y lo
  escribe en un WAV.

Suponiendo que JACK y natambio ya están en marcha (natambio con la configuración
de medición, exponiendo sus puertos `natambio:*_input_*`), una medida de la vía
*front left* sería:

```sh
ecasound -t:10 \
    -a:1 -i sweep_48k.wav -a:1 -o:jack_auto,natambio:front_input_left -a:1 -eadb:0 \
    -a:2 -i:jack_auto,system:capture_1 -a:2 -f:f32_le,1,48000 \
    -o:left_sweep_1.wav -a:2 -eadb:10 -ev
```

Desglose de los parámetros (idénticos a los del script):

| Parámetro | Significado |
|---|---|
| `-t:10` | duración de la captura en segundos (`REC_SECONDS`) |
| `-a:1` | cadena 1 = **reproducción** del sweep |
| `-i sweep_48k.wav` | entrada de la cadena 1: el fichero del sweep (`SWEEP`) |
| `-o:jack_auto,natambio:front_input_left` | salida de la cadena 1: autoconecta a la entrada de natambio de esa vía (`OUT_PORTS`) |
| `-eadb:0` (cadena 1) | ganancia de **salida** en dB (`GAIN_OUT`) |
| `-a:2` | cadena 2 = **grabación** del micrófono |
| `-i:jack_auto,system:capture_1` | entrada de la cadena 2: autoconecta a la toma de micrófono (`IN_MEAS`) |
| `-f:f32_le,1,48000` | formato del WAV grabado: float 32-bit, mono, 48 kHz |
| `-o:left_sweep_1.wav` | salida de la cadena 2: el WAV con la respuesta grabada |
| `-eadb:10` (cadena 2) | ganancia de **entrada** en dB (`GAIN_IN`) |
| `-ev` | analiza picos al terminar (útil para ver si hubo clipping) |

El `jack_auto` es lo que hace la **autoconexión**: `ecasound` crea su puerto JACK y
lo conecta automáticamente al puerto indicado (la entrada de natambio en la
reproducción, la captura del micrófono en la grabación).

El parámetro ``-ev`` se incluye para que ecasound genere una tabla de niveles medidos de forma que se pueda analizar y si el pico y el promedio de la señal están muy bajo o muy cerca de saturación.

Para medir **otra vía** basta con cambiar el puerto de salida (p. ej.
`natambio:front_input_right`) y el nombre del WAV. Y para medir **sin pasar por
natambio** (directamente a un altavoz de la tarjeta), se sustituye la salida por
el puerto físico correspondiente, p. ej. `-o:jack_auto,system:playback_1`.

Esta captura es todavía el *sweep grabado*: para obtener la **respuesta impulsiva**
hay que deconvolucionarla con el sweep inverso, que es justo lo que hace el script
`fft_convolve.py` (Fase 2 del flujo automatizado):

```sh
python fft_convolve.py left_sweep_1.wav inverse_48k.wav front_left_impulse_1.wav
```

### ¿Es correcta la medida?

Existen una serie de comprobaciones básicas para asegurar que la medida tomada es correcta y garantiza la obtención de una impulsiva correcta:

1. Por los altavoces debe sonar el log-sweep a un nivel razonablemente alto, quizás un poco por encima del habitual que tienen cuando se escucha música. Pero sin necesidad de llegar a resultar muy molesto.
2. En los valores del análsis de ecasound de -ev,m conviene tener un headroom de 10 dB, con lo que **[Pendiente de obtener un ejemplo de output]**

El script herramienta de pca4drc ``check_capture.py`` permite hacer la comprobación de los niveles correctos a partir del propio análisis del wav de la medida, antes de convertirlo a impulsiva. Este análisis incluye la comprobación de que no haya habido clipping de nivel y que la SNR sea suficiente (típicamente 20 dB).

### Tabla de scripts y comandos básicos para medir manualmente

Resumen de las herramientas que intervienen en una medida manual, en el orden en
que se usan. Los scripts `sweepgen.py` y `fft_convolve.py` forman parte del
toolkit [`tools/python_pca4drc/`](../tools/python_pca4drc/README.md); `ecasound`
es una herramienta externa del sistema.

| Paso | Herramienta | Pertenece a | Qué hace | Ejemplo |
|---|---|---|---|---|
| 1. Generar el sweep | `sweepgen.py` | `tools/python_pca4drc/` | Genera el log-sweep de excitación y su filtro inverso a partir del XML `<generate_sweep>` | `python sweepgen.py sweep.xml -s sweep_48k.wav -i inverse_48k.wav` |
| 2. Medir (reproducir y grabar) | `ecasound` | externo (sistema) | Reproduce el sweep por una vía y graba la respuesta del micrófono en un WAV | `ecasound -t:10 -a:1 -i sweep_48k.wav -a:1 -o:jack_auto,natambio:front_input_left -a:1 -eadb:0 -a:2 -i:jack_auto,system:capture_1 -a:2 -f:f32_le,1,48000 -o:left_sweep_1.wav -a:2 -eadb:10 -ev` |
| 2b. Validar la captura (opcional) | `check_capture.py` | `tools/python_pca4drc/` | Analiza el WAV grabado y avisa de clipping, nivel bajo o SNR baja antes de seguir | `python check_capture.py left_sweep_1.wav "front left"` |
| 3. Obtener la impulsiva | `fft_convolve.py` | `tools/python_pca4drc/` | Deconvoluciona el sweep grabado con la inversa para obtener la respuesta impulsiva | `python fft_convolve.py left_sweep_1.wav inverse_48k.wav front_left_impulse_1.wav` |

#### Uso de `check_capture.py`

Tras cada grabación conviene comprobar que los niveles son correctos **antes** de
procesarla para obtener la impulsiva, o de mover el micrófono si estamos en un proceso de medida multipunto. Eso es lo que hace `check_capture.py`: lee
el WAV capturado y avisa de tres problemas habituales.

```sh
python check_capture.py <wav> [etiqueta] [--min-level -40] [--min-snr 20]
```

- `<wav>`: el fichero de la captura a analizar (p. ej. `left_sweep_1.wav`).
- `[etiqueta]` (opcional): texto para identificar la captura en el mensaje (p. ej.
  `"front left"`).
- `--min-level` (por defecto `-40` dBFS): umbral por debajo del cual avisa de
  **nivel bajo**.
- `--min-snr` (por defecto `20` dB): umbral por debajo del cual avisa de **SNR
  baja**.

Qué comprueba:

- **Clipping**: si el pico llega a ~0 dBFS (≥ 0.999). Hay que **bajar** la ganancia.
- **Nivel bajo**: si el pico queda por debajo de `--min-level`. Hay que **subir** la
  ganancia (del previo de micrófono y/o `GAIN_IN`).
- **SNR baja**: relación señal/ruido por debajo de `--min-snr`. La estima
  comparando el RMS de toda la captura con el del **silencio inicial** (los
  primeros 50 ms, antes de que llegue el sweep); por eso es importante dejar el
  `<silence>` inicial al generar el sweep. Una SNR baja suele indicar demasiado
  ruido de fondo o nivel de sweep insuficiente.

El resultado se imprime en una línea, por ejemplo:

```
    [front left] pico -6.2 dBFS, SNR~48 dB -> OK
    [front left] pico -0.0 dBFS, SNR~45 dB -> *** AVISO: CLIPPING (pico -0.01 dBFS) ***
```

Además devuelve un **código de salida** útil para automatizar: `0` si la captura
es válida y `1` si hay algún aviso (o el WAV no se puede leer / está vacío), de
modo que `measure_pca4drc.sh` lo usa para no avanzar y pedir repetir la medida.
Por ejemplo, en un script propio:

```sh
if python check_capture.py left_sweep_1.wav "front left"; then
    echo "Captura válida; continúo con la deconvolución."
else
    echo "Niveles incorrectos: reajusta la ganancia y repite la medida."
fi
```

### Aplicación de DRC-FIR

Una vez obtenida la respuesta impulsiva de referencia de cada canal, el último
paso es **invertirla** para generar los filtros FIR de corrección. La aplicación recomendada para el caso de NatAmbio es [DRC-FIR](https://drc-fir.sourceforge.net/).

#### Ficheros que usa DRC


DRC se controla con un fichero de configuración, [`config.drc`](../tools/python_pca4drc/config.drc), con un juego de parámetros enorme y muy flexible. La documentación de DRC es exhaustiva, con lo que en estas secciones dedicadas a este programa se hará un resumen de su aplicación a NatAmbio.

A la hora de emplear DRC con el objetivo de obtener filtros de ecualización aplicables a DRC, los parámetros básicos de su ejecución son:

| Fichero | Clave en `config.drc` | Qué es | ¿Incluido? |
|---|---|---|---|
| Impulso de **entrada** | `BCInFile` (`pca4drc/PCA_0.raw`, o el impulso medido con 1 sola toma) | La respuesta impulsiva de referencia a invertir | Lo genera la medición (Fases 2–3) |
| Curva **objetivo** (*target*) | `PSPointsFile` (`../target/48.0 kHz/subultra-48.0.txt`) | La respuesta de frecuencia deseada tras la corrección | Sí, en [`tools/python_pca4drc/target/<frecuencia>/`](../tools/python_pca4drc/target/) |
| Corrección de **micrófono** | `MCPointsFile` (`wm-61a.txt`) | Curva de calibración del micrófono, para descontar su respuesta | **No**; además viene **desactivada** por defecto |
| Filtros FIR de **salida** | `PSOutFile` (`rps.raw`), `MSOutFile` (`rms.raw`) | Los filtros de corrección resultantes (fase mínima / fase lineal) | Los genera DRC |

Notas importantes:

- **Target**: el toolkit incluye un juego de curvas objetivo por frecuencia de
  muestreo (`target/44.1 kHz/`, `target/48.0 kHz/`, etc.). Hay que elegir la del
  *sample rate* de la medida (48 kHz aquí) y el perfil deseado (`flat-48.0.txt`,
  `bk-48.0.txt`, `subultra-48.0.txt`, …) editando `PSPointsFile` en `config.drc`.
- **Micrófono**: en el `config.drc` por defecto la etapa de compensación de
  micrófono está **apagada** (`MCFilterType = N`, `MCNumPoints = 0`), por lo que
  `wm-61a.txt` no se usa tal cual. Si tu micrófono trae curva de calibración (ver
  [Micrófonos de medida](#micrófonos-de-medida)), colócala con ese formato,
  apúntala en `MCPointsFile` y activa la etapa (`MCFilterType` a fase mínima/lineal
  y `MCNumPoints` al nº de puntos) para descontar la respuesta del micrófono.

#### Paso de wav a raw

drc solo lee ficheros audio en formato raw, con lo que los ficheros wav de la impulsivas hay que convertirlos. Y posteriormente los filtros generados por drc en formato raw hay que convertirlos a wav para hacerlos compatibles con NatAmbio. Para ello en pca4drc existen dos herramientas muy sencillas:
```
    python raw2wav.py <raw> [<raw> ...] [--rate 48000]
    python raw2wav.py impulses/*.raw --rate 48000
```
```
    python wav2raw.py <wav> [<wav> ...]
    python wav2raw.py impulses/*.wav
```

#### Cómo se invoca

La invocación manual de `drc` para obtener el filtro FIR DRC para cada altavoz es:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw config.drc
```

`--BCBaseDir` fija la carpeta de impulsos de la vía (de modo que las rutas
relativas del config, como `../target/...`, sigan resolviendo) y `--BCInFile` el
impulso de entrada. DRC deja los filtros `rps.raw` (paso mínimo) y `rms.raw` (paso
lineal) en esa carpeta; el script los pasa a WAV con `raw2wav.py`. Esos WAV son
los filtros FIR que después carga el convolver (NatAmbio u otro) para la
corrección de sala.

Si se quiere modificar el target de config.drc se puede invocar con:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw --PSPointsFile=new_target.txt config.drc
```

Y si se quiere incluir un fichero de calibración de micrófono se invoca mediante:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw --MCFilterType=L --MCPointsFile=calibration.txt config.drc
```
Ambas opciones se pueden combinan incluyendo todos los parámetros

## Proceso manual vs proceso por script

Una vez descrito un paso de medida, el proceso consiste en medir para diferentes altavoces y, si estamos midiendo en multipunto, para diferentes posiciones del micrófono. Esto requiere un orden mental con los nombres de los ficheros WAV identificables y/o organizados por carpetas para cada altavoz.

Seguir este orden es más sencillo si se utiliza un script herramienta que se proporciona en tools/python_pca4drc llamado ``measure_pca4drc.sh``. 

El script measure_pca4drc.sh permite controlar la ejecución de:

1. Generación del log-sweep.
2. Calibración de ganancias de entrada y salida.
3. El propio proceso de medida.
4. El control de calidad de las medidas.


A continuación se explicará la aplicación del citado script en cada posible sistema NatAmbio:

1. Un solo dipolo sin subwoofer.
2. Un dipolo con subwoofer.
3. Dos dipolos sin subwoofer.
4. Dos dipolos con subwoofer.


### Caso de un solo dipolo, sin subwoofer, una única medida por canal

En el caso más simple, un sistema estéreo básico, que se quiere convertir en un NatAmbio de un solo dipolo y se quiere aplicar filtros DRC a partir de una única medida por canal.

Estas condiciones se trasladan a tres parámetros inline de
[`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh), antepuestos a
la llamada:

- `FULL_NATAMBIO=false` → un solo dipolo: mide sólo dos vías, *front left* y
  *front right* (usa `half_natambio_measurements_normal.xml`).
- `SUBWOOFER=false` → sin subwoofer. Es el valor por defecto, así que puede
  omitirse.
- `NUM_POS=1` → una única medida por canal. Con una sola medida **no se aplica
  PCA**: el impulso medido se usa directamente como entrada de DRC.

La ejecución completa (las cinco fases: sweep → medición → impulsos → DRC) queda:

```sh
FULL_NATAMBIO=false NUM_POS=1 ./measure_pca4drc.sh
```

O, dejando explícito el `SUBWOOFER=false` por claridad:

```sh
FULL_NATAMBIO=false SUBWOOFER=false NUM_POS=1 ./measure_pca4drc.sh
```

Conviene **calibrar antes los niveles** (siguiente apartado) y reutilizar las
ganancias recomendadas en esta llamada, p. ej.:

```sh
FULL_NATAMBIO=false NUM_POS=1 GAIN_OUT=-3 GAIN_IN=12 ./measure_pca4drc.sh
```

### calibración

Antes de tomar ninguna medida buena conviene fijar unas ganancias de
reproducción (`GAIN_OUT`) y de captura (`GAIN_IN`) que sirvan **a la vez** para
las dos vías del dipolo (front izquierdo y derecho), sin clipping, con nivel
suficiente y buena relación señal/ruido. Para esto el script
[`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh) dispone de un
**modo calibración** (`CALIBRATE=1`) que se limita a reproducir el sweep y grabar
para ajustar niveles: no extrae impulsos, ni hace PCA, ni DRC (desactiva esas
fases para no exigir sus dependencias).

Para un sistema de **un solo dipolo, sin subwoofer**, el modo se selecciona con:

- `FULL_NATAMBIO=false` → sólo dos vías, *front left* y *front right* (un dipolo).
- `SUBWOOFER=false` → sistema sin subwoofer (es el valor por defecto, puede omitirse).
- `CALIBRATE=1` → modo calibración de ganancias.

```sh
cd <directorio_de_trabajo>
FULL_NATAMBIO=false CALIBRATE=1 ./measure_pca4drc.sh
```
En el momento de calibrar conviente partir con unas ganancias iniciales sustancialmente más bajas que las de por defecto
(`GAIN_OUT=0` dB, `GAIN_IN=10` dB), lo cual se puede hacer anteponiendo sus variables igualmente a la llamada:

```sh
FULL_NATAMBIO=false CALIBRATE=1 GAIN_OUT=-10 GAIN_IN=5 ./measure_pca4drc.sh
```

Qué hace el modo calibración, paso a paso:

1. Genera el sweep y su inversa (Fase 0), salvo que se salte con `DO_SWEEP=0`
   reutilizando un par ya existente.
2. Arranca `natambio` con la configuración `half_natambio_measurements_normal.xml`
   (medio sistema = un dipolo, sin subwoofer) y muestra el **informe de enrutado**
   (qué salida de natambio va a cada salida física de la tarjeta). Se debe confirmar con
   Enter que la asignación es correcta.
3. Reproduce el sweep por **cada vía** (front L y front R) a las ganancias
   definidas en ese momento, graba la captura del micrófono y la analiza con `check_capture.py`,
   avisando de **clipping**, **nivel bajo** (`MIN_LEVEL`, −40 dBFS por defecto) o
   **SNR baja** (`MIN_SNR`, 20 dB).
4. Tras probar las dos vías, si alguna no cumple los requisitos, se puede repetir el proceso con **dos valores nuevos de** `GAIN_OUT GAIN_IN` (p. ej. `-3 12`), además de retocar la
   ganancia física del previo de micrófono. Cuando ambas vías dan niveles
   correctos, solo hay que pulsar Enter para aceptar.
5. Al terminar, natambio se detiene y el script imprime las **ganancias
   recomendadas**, listas para usarlas en la medición real, por ejemplo:

   ```sh
   GAIN_OUT=-3 GAIN_IN=12 FULL_NATAMBIO=false ./measure_pca4drc.sh
   ```

> Durante la calibración conviene ajustar primero la ganancia **física** del previo de
> micrófono y dejar el ajuste fino para `GAIN_OUT`/`GAIN_IN`. Y vigilar en todo
> momento el nivel de reproducción para evitar accidentes (ver el aviso de
> [Antes de medir](#antes-de-medir)).



## pendiente de revisar

El script repite todo el proceso para cada **vía** (altavoz) del sistema:

- **NatAmbio completo** (`FULL_NATAMBIO=true`, por defecto): cuatro vías —
  `front_left`, `front_right`, `rear_left`, `rear_right`.
- **Sistema de dos altavoces** (`FULL_NATAMBIO=false`): sólo `front_left` y
  `front_right`.

## Requisitos previos

- Un **servidor JACK** en marcha a la frecuencia de captura (48000 Hz).
- **`ecasound`** (reproducción del sweep y grabación de la respuesta).
- **`natambio`** compilado/instalado: durante la medición se arranca con una
  configuración específica para enrutar el sweep a cada vía.
- **`drc`** (DRC-FIR de Sbragion) en el `PATH`, para la fase de corrección.
- Las dependencias Python del toolkit: `pip install -r tools/python_pca4drc/requirements.txt`
  (`numpy`, `scipy`, `soundfile`).
- Un **micrófono de medición** con su previo, conectado a la entrada de la tarjeta
  (`IN_MEAS`, por defecto `system:capture_1`).

## Las cinco fases

El script ejecuta cinco fases encadenadas. Cada una puede activarse o saltarse de
forma independiente con su interruptor `DO_*` (ver más abajo).

### Fase 0 — Generación del sweep (`sweepgen.py`)

Genera el barrido logarítmico de excitación (`SWEEP`, por defecto `sweep_48k.wav`)
y su filtro inverso (`INVERSE`, `inverse_48k.wav`), que luego se usa para
deconvolucionar. Los parámetros del barrido se controlan con `SWEEP_RATE`
(48000 Hz, **debe** coincidir con la captura), `SWEEP_LENGTH` (6 s), `SWEEP_HZSTART`
/ `SWEEP_HZEND` (20–20000 Hz), `SWEEP_AMPLITUDE` (0.5), etc.

Si ya dispones de un par sweep/inversa, salta esta fase con `DO_SWEEP=0`.

### Fase 1 — Medición (`ecasound`)

Por cada vía y por cada posición de micrófono reproduce el sweep y graba la
respuesta:

1. Arranca **natambio** con la configuración correspondiente según `FULL_NATAMBIO`
   (full/half) y `SUBWOOFER` (subwoofer/normal):
   `{full,half}_natambio_measurements_{subwoofer,normal}.xml`. Su salida se
   redirige a `NATAMBIO_LOG` (`/tmp/natambio_measure.log`) y se comprueba que
   registra sus puertos JACK; si no, aborta.
2. Imprime un **informe de configuración** (modo, vías a medir, enrutado real de
   las salidas de natambio a las salidas físicas `system:playback_*`) y pide
   **confirmación** antes de empezar.
3. `ecasound` envía el sweep a la entrada de natambio (`natambio:front_input_left`,
   …) y graba la captura del micrófono, con `GAIN_OUT` dB a la salida y `GAIN_IN`
   dB a la entrada, durante `REC_SECONDS` (10 s).
4. Tras cada captura, `check_capture.py` valida el WAV y avisa de **clipping**,
   **nivel bajo** (`MIN_LEVEL`, −40 dBFS) o **SNR baja** (`MIN_SNR`, 20 dB). Si los
   niveles no son válidos, no avanza: pide reajustar la ganancia del previo de
   micrófono y repetir la medida (salvo en modo `AUTO=1`).

Al terminar, natambio se detiene automáticamente.

> Las medidas no deben tomarse en puntos coincidentes entre campañas, sino en
> posiciones distintas dentro de la misma región de escucha (un área de radio
> reducido alrededor del punto de escucha), manteniendo la sala en las mismas
> condiciones.

### Fase 2 — Impulsos (`fft_convolve.py`)

Deconvoluciona cada sweep grabado con el sweep inverso para obtener la respuesta
impulsiva de cada posición.

### Fase 3 — PCA (`pca4drc.py`)

Por cada vía, calcula la descomposición PCA del conjunto de impulsos y guarda las
componentes en `i_<via>/pca4drc/` (`PCA_0.wav` = componente principal, `PCA_1.wav`,
…), de longitud `OUTPUT_LEN` (131072 muestras). A continuación las convierte a
`.raw` (float 32-bit LE) con `wav2raw.py` para alimentar a DRC. El parámetro
`PCA_NORMALIZE` (por defecto `true`) normaliza las componentes al pico de la
principal.

### Fase 4 — DRC (`drc` de Sbragion)

Por cada vía ejecuta `drc` con `config.drc` (junto al script), usando la
componente principal `pca4drc/PCA_0.raw` como impulso de entrada. Genera los
filtros de corrección `rps.raw` / `rms.raw` y los convierte a WAV con
`raw2wav.py`. Activable con `DO_DRC` (1 por defecto); si una vía falla, avisa y
continúa con las demás.

## Estructura de directorios generada

Ejecutando el script en un directorio de trabajo, se crean (para NatAmbio
completo):

```
m_front_left/   m_front_right/   m_rear_left/   m_rear_right/    # sweeps grabados (Fase 1)
i_front_left/   i_front_right/   i_rear_left/   i_rear_right/    # impulsos (Fase 2)
    └── pca4drc/    PCA_0.wav, PCA_1.wav, …  +  PCA_0.raw, …     # componentes PCA (Fase 3)
    └── rps.raw, rms.raw  (+ sus .wav)                          # filtros DRC (Fase 4)
```

## Uso y configuración

Todas las variables tienen un valor por defecto, pero pueden **sobrescribirse al
vuelo** anteponiéndolas a la llamada (sin editar el script):

```sh
./measure_pca4drc.sh                       # las cinco fases, interactivo (4 vías, normal)
FULL_NATAMBIO=false ./measure_pca4drc.sh   # sistema de 2 altavoces (sólo front L/R)
SUBWOOFER=true ./measure_pca4drc.sh        # arranca natambio con la config de subwoofer
NUM_POS=8 ./measure_pca4drc.sh             # 8 posiciones de micrófono en vez de 16
AUTO=1 ./measure_pca4drc.sh                # sin pausas interactivas
DO_SWEEP=0 ./measure_pca4drc.sh            # usar un sweep/inversa ya existentes
DO_MEASURE=0 ./measure_pca4drc.sh          # re-procesar lo ya medido (saltar la medición)
DO_DRC=0 ./measure_pca4drc.sh             # todo menos la corrección DRC
DO_MEASURE=0 DO_IMPULSES=0 DO_PCA=0 ./measure_pca4drc.sh  # sólo DRC sobre PCA_0.raw ya generados
```

Los interruptores de fase `DO_SWEEP` / `DO_MEASURE` / `DO_IMPULSES` / `DO_PCA` /
`DO_DRC` valen `1` (activada) o `0` (saltada) y son independientes, de modo que
pueden combinarse para ejecutar sólo las fases que interesen.

Variables más habituales:

| Variable | Por defecto | Significado |
|---|---|---|
| `FULL_NATAMBIO` | `true` | `true` = 4 vías (front+rear); `false` = 2 (front) |
| `SUBWOOFER` | `false` | Config de natambio con/sin subwoofer |
| `NUM_POS` | `16` | Número de posiciones de micrófono |
| `IN_MEAS` | `system:capture_1` | Puerto JACK de captura del micrófono |
| `SELECT_INPUT` | `0` | `1` = elegir `IN_MEAS` por menú interactivo antes de medir |
| `GAIN_OUT` / `GAIN_IN` | `0.0` / `10.0` dB | Ganancia de reproducción / captura |
| `REC_SECONDS` | `10` | Duración de cada captura (s) |
| `MIN_LEVEL` / `MIN_SNR` | `-40` dBFS / `20` dB | Umbrales de validación de la captura |
| `OUTPUT_LEN` | `131072` | Longitud de las componentes PCA (muestras) |
| `PCA_NORMALIZE` | `true` | Normalizar las componentes al pico de la principal |
| `DRC_CONFIG` | `config.drc` | Configuración de DRC-FIR |
| `AUTO` | `0` | `1` = sin pausas interactivas |

La lista completa de variables está documentada en
[`tools/python_pca4drc/README.md`](../tools/python_pca4drc/README.md). Si los
scripts `.py` no están junto al `.sh`, exporta `TOOLS_DIR` apuntando a ellos.

## Flujo de trabajo recomendado

1. **Prepara la sala y el sistema**: JACK en marcha a 48 kHz, micrófono colocado
   en la primera posición, niveles de previo razonables.
2. **Primera ejecución completa** e interactiva: `./measure_pca4drc.sh`. Revisa el
   informe de configuración (enrutado de vías) antes de confirmar.
3. **Ajusta la ganancia** si `check_capture.py` avisa de nivel/SNR, y repite la
   posición.
4. Una vez medidas todas las posiciones y vías, las fases 2–4 generan impulsos,
   PCA y filtros DRC sin más intervención.
5. Para **re-procesar** sin volver a medir (p. ej. probando otra curva objetivo o
   parámetros de PCA), repite con `DO_SWEEP=0 DO_MEASURE=0`.
