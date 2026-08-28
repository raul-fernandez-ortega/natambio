# xtc_filters

Generadores de línea de comandos independientes para filtros FIR de **XTC**
(cancelación de diafonía), en dos variantes:

| Herramienta | Disposición | Filtros que escribe |
|---|---|---|
| `natambio-xtc-filters` | simétrica | directo + cruzado |
| `natambio-xtc-filters-asym` | asimétrica | directo (compartido) + cruzado izquierdo + cruzado derecho |

Ambas escriben ficheros WAV de coma flotante de 32 bits en `./filters/`, y ambas
reutilizan el código de diseño de filtros compartido en `../../lib` (`xtc.c` y
`xtc_asym.c` → `dsp.c`, `binaural_cues.c`) —las mismas unidades que enlaza
`natambio`—, de modo que el DSP no está duplicado. Solo los dos programas
principales y las unidades de apoyo locales (lector TOML, esquema de
configuración, escritor de WAV) son específicos de estas herramientas.

## Compilación

De forma independiente (sin autotools):

```sh
make -f Makefile.simple
# opcional: volcar también los filtros intermedios ILD_*.wav / MP_ILD_*.wav
make -f Makefile.simple DEBUG=1
```

O como parte de la compilación general con autotools (`./autogen.sh &&
./configure && make` desde la raíz del proyecto), que instala ambas herramientas
en `$(bindir)`.

## Ficheros de configuración

Los parámetros se dan en un fichero TOML. Aquí se incluyen cuatro ejemplos
comentados:

| Fichero | Qué muestra |
|---|---|
| `xtc_sym_default.toml` | simétrico, reproduciendo los valores por defecto |
| `xtc_sym_wide.toml` | simétrico, ajustado para una imagen más amplia |
| `xtc_asym_geometry.toml` | colocación asimétrica de los altavoces |
| `xtc_asym_room.toml` | colocación simétrica en una sala acústicamente asimétrica |

Instalados desde los paquetes Debian, quedan en
`/usr/share/doc/natambio-tools/examples/`.

El esquema usa los mismos nombres de clave para un lado en las dos herramientas,
de modo que `[xtc]`, `[left]` y `[right]` son bloques intercambiables:

```toml
sample_rate = 48000
filter_len  = 4096
frac_delay  = true    # opcional; ITD exacto en vez de redondeado (ver abajo)
model_delay = 64      # opcional; retardo global del camino fraccionario

[xtc]                 # [left] y [right] en la herramienta asimétrica
itd_us      = 170     # diferencia interaural de tiempo, microsegundos
ild_db      = 14.0    # diferencia interaural de nivel por escalón, dB positivos
ild_alpha   = 2.0     # factor de escala del modelo empírico del espectro ILD
azimuth_deg = 20      # semiángulo entre los altavoces

[output]
directory = "filters"
prefix    = "mi_sala" # opcional
```

El lector acepta un **subconjunto** estricto de TOML: tablas, claves simples,
enteros, flotantes, cadenas entrecomilladas, booleanos y comentarios. Los
arrays, las tablas inline, las claves con punto y lo demás se rechazan indicando
el número de línea en vez de interpretarse mal, y una clave desconocida es un
error, así que una errata como `ild_alfa` falla en lugar de dejar silenciosamente
`ild_alpha` en su valor por defecto. Como la sintaxis aceptada es un
subconjunto, todos estos ficheros son TOML válido y las herramientas Python de
[`../python_xtc_filters`](../python_xtc_filters) los leen igual, con el
`tomllib` estándar.

## Ejecución

Simétrica, por TOML, por flags, o ambos:

```sh
./xtc_filters -c xtc_sym_default.toml
./xtc_filters -t ITD_us -l ILD_dB -a ILD_alpha -z azimut_grados -r frec_muestreo -f longitud
./xtc_filters -c xtc_sym_default.toml -l 12      # el fichero, con un valor cambiado
# valores por defecto: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096 -M 64
# -F : ITD fraccionario (ver abajo);  -M N : su retardo de modelado
```

La interfaz de flags no ha cambiado respecto a versiones anteriores. `-c` se lee
en el punto de la línea de comandos donde aparece, de modo que los flags
colocados **después** sobrescriben el fichero y los colocados antes no.

Asimétrica, solo por TOML:

```sh
./xtc_filters_asym -c xtc_asym_geometry.toml
```

La herramienta asimétrica no tiene interfaz de flags a propósito: ocho números en
una línea de comandos, la mitad de ellos distinguiéndose de la otra mitad por una
sola letra, es exactamente la forma de error que produce un filtro verosímil para
la geometría equivocada.

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
[`../python_xtc_filters/compare_frac_delay.py`](../python_xtc_filters/compare_frac_delay.py).
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

Simétrica, sin `output.prefix` (el contrato histórico de nombres):

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
```

Con un prefijo configurado, en cualquiera de las dos:

```
<prefijo>_direct.wav
<prefijo>_cross.wav                      # simétrica
<prefijo>_cross_left.wav                 # asimétrica
<prefijo>_cross_right.wav                # asimétrica
```

La herramienta asimétrica escribe **tres** filtros, no cuatro: los dos filtros
directos del modelo son idénticos, ya que dependen únicamente del operador de
ida y vuelta *P = G_l ∗ G_r*, que es simétrico bajo intercambio de canales. Lo
que difiere entre canales es el filtro cruzado.

Atención a qué lado alimenta a qué altavoz. `<prefijo>_cross_left.wav` alimenta
el altavoz **izquierdo** pero se construye con los parámetros del lado
**derecho**, porque cancela la fuga del altavoz derecho hacia el oído izquierdo y
el único camino directo que llega a ese oído es el del altavoz izquierdo. Todo en
esa rama es «derecho» —el ITD, el ILD y el espectro— excepto el altavoz que la
radia.

## El balance, en el caso asimétrico

Estos coeficientes son **M⁻¹**: el ajuste de nivel entre canales no se hornea en
ellos deliberadamente, igual que en el bloque `<xtc_asym>` de `natambio`. Hay que
aplicarlo después, como ganancia sobre las dos convoluciones que alimentan la
misma salida, atenuando el canal que llega más fuerte y sin amplificar nunca el
otro.

No es un detalle cosmético. Sin ajustar, los términos cruzados no se anulan y la
cancelación alcanzable queda acotada en torno a 20·log₁₀|1−b|: un error de 1 dB
sitúa el techo cerca de −19 dB, y uno de 3 dB cerca de −11 dB. El procedimiento
de escucha y el razonamiento completo están en el apartado del balance de
[`docs/xtc/xtc_no_simetrico_es.md`](../../docs/xtc/xtc_no_simetrico_es.md).

## Relacionado

- Los mismos filtros los puede generar `natambio` en proceso mediante bloques
  `<xtc>` y `<xtc_asym>` (ver `docs/README.CONFIG`); estas herramientas sirven
  para producirlos offline como ficheros WAV.
- Equivalentes en Python puro, que leen los mismos ficheros TOML:
  [`../python_xtc_filters`](../python_xtc_filters).
- El modelo: [`docs/xtc/xtc_filters_es.md`](../../docs/xtc/xtc_filters_es.md)
  (simétrico) y
  [`docs/xtc/xtc_no_simetrico_es.md`](../../docs/xtc/xtc_no_simetrico_es.md)
  (asimétrico).
