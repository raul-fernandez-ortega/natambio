# XTC: ITD fraccionario

Rama `pan_scale` (2026-08-28). Ficheros tocados: `lib/dsp.c`, `lib/dsp.h`,
`lib/xtc.c`, `lib/xtc.h`, `lib/xtc_asym.c`, `lib/xtc_asym.h`,
`lib/test_xtc_asym.c`, `src/naconf.cpp`, `src/structs.hpp`,
`tools/xtc_filters/{main.c,main_asym.c,xtc_conf.c,xtc_conf.h,*.toml,README*.md}`,
`tools/python_xtc_filters/{xtc_filters.py,xtc_filters_asym.py,xtc_conf.py,compare_frac_delay.py,Makefile.am,requirements.txt,README*.md}`,
`src/README.md`, `docs/README.CONFIG`.

**Aviso para el port [NatAmbio-VST3](https://github.com/digitalfrost84/NatAmbio-VST3):
`lib/xtc.c` deja de estar congelado.** Hasta ahora `lib/xtc_asym.c` duplicaba a
propósito el modelo ILD para no tocarlo, porque el port lo replica byte a byte
(ver el comentario de cabecera de `xtc_asym.h` y `lib/Makefile.am`). Este cambio
levanta ese congelado: `xtc.c` gana una función y `process()` gana dos
argumentos. Los dos ficheros siguen siendo implementaciones independientes del
mismo modelo, que es lo que `test_xtc_asym.c` contrasta.

---

## 1. El problema

La recursión XTC coloca sus taps escribiendo en un índice de array, así que el
ITD tiene que redondearse antes:

    lib/xtc.c:217        int itd_samples = (int)round(itd_exact);
    lib/xtc_asym.c:285   igual, una vez por lado

Ese redondeo no es gratis. Si la cancelación llega con un error de retardo `dt`,
la señal cancelante llega con un error de fase `2πf·dt` y el residuo relativo es

    residuo = |1 − e^{−jθ}| = 2·sin(π·f·dt)

A 48 kHz, con los valores por defecto (`itd_us = 170` → 8,16 muestras → 8, error
0,16 muestras = 3,33 µs):

| f | tope de cancelación |
|---|---|
| 5 kHz | −19,6 dB |
| 10 kHz | −13,6 dB |
| 15 kHz | −10,1 dB |

Subir el sample rate **no** lo arregla: a 96 kHz, 170 µs son 16,32 muestras y el
error sigue siendo los mismos 3,33 µs. Solo mejora el caso peor (10,4 → 5,2 µs).

El caso asimétrico es peor: `delay_l` y `delay_r` se redondean por separado y el
periodo de ida y vuelta `T = delay_l + delay_r` hereda ambos errores, así que una
geometría puede arrastrar hasta una muestra entera de error de periodo. Con el
ejemplo de la documentación (180 µs y 140 µs a 48 kHz = 8,64 y 6,72 muestras,
redondeadas a 9 y 7) el periodo sale 16 en vez de 15,36.

## 2. La corrección

Evaluar la misma recursión en frecuencia, donde un tap es un factor de fase
lineal `exp(-j2πfτ)` —el operador de retardo limitado en banda exacto— en lugar
de un índice. No hay kernel de interpolación ni compromiso precisión/longitud.

Se descartó la alternativa obvia, un sinc enventanado en tiempo, por dos razones:
los kernels enventanados **no** se componen exactamente, de modo que el error
crecería a lo largo de la escalera; y con τ ≈ 8 muestras la cola no causal del
primer tap cruzado no cabe antes de n = 0.

**Iteración por iteración, misma escalera.** Mismos taps, mismas ganancias, mismo
orden de Horner (insertar el tap, luego filtrar todo el acumulador con el ILD).
Lo único que cambia es cómo se coloca un tap. El modelo ILD, el paso a fase
mínima y las normalizaciones no se tocan.

Un detalle que parecía un obstáculo y no lo era: `get_xtc()` trunca el
acumulador a `length` en cada iteración y la versión espectral no. Da igual —
todo lo posterior son convoluciones causales, así que una muestra en n ≥ length
no puede plegarse nunca sobre n < length. Truncar en cada pasada y truncar una
sola vez al final dejan los mismos primeros `length` taps.

### Nuevo en `lib/dsp.c` / `dsp.h`

| Función | Qué hace |
|---|---|
| `dsp_next_pow2()` | expone el `next_pow2()` que ya era privado |
| `dsp_rfft()` / `dsp_irfft()` | transformadas de medio espectro; el par (re, im) va en dos arrays reales para que `dsp.h` no arrastre tipos de FFTW |
| `dsp_spectrum_add_delayed()` | `acc += gain · exp(-j2πf·delay)`, con `delay` fraccionario |
| `dsp_spectrum_mul()` | producto complejo punto a punto |

Las dos últimas son una iteración de Horner sobre espectros, y existen para que
el bucle trigonométrico y la regla del bin de Nyquist vivan en un solo sitio en
vez de en las tres recursiones.

`DSP_MAX_LEN` pasa de `dsp.c` a `dsp.h`: quien dimensiona su propia transformada
necesita la misma cota.

### Nuevo en `lib/xtc.c` / `lib/xtc_asym.c`

    int get_xtc_frac(int length, double attenuation, double delay,
                     const double *ild_filter, int model_delay,
                     double *direct_out, double *cross_out);

    int get_xtc_asym_frac(int length, double att_l, double att_r,
                          double delay_l, double delay_r,
                          const double *ild_mean, const double *ild_l, const double *ild_r,
                          int model_delay,
                          double *direct_out, double *cross_left_out, double *cross_right_out);

`process()` y `process_asym()` ganan `int frac_delay, int model_delay` antes de
los buffers de salida. **Es un cambio de firma**, no una función nueva: hay que
actualizar los puntos de llamada (`src/naconf.cpp`, `tools/xtc_filters/main*.c`,
`lib/test_xtc_asym.c`).

### El bin de Nyquist

Un espectro real de longitud par tiene el bin de Nyquist real, cosa que un
desplazamiento fraccionario viola. `dsp_spectrum_add_delayed()` se queda con la
parte real ahí; es la resolución estándar, da el interpolador sinc periódico y el
error queda confinado a ese único bin, donde además el shelf de 20 kHz ya ha
dejado `|A|` en ~0,02.

### `model_delay`

Un desplazamiento fraccionario tiene respuesta impulsional de dos lados, así que
el primer tap cruzado lleva energía a tiempo negativo. `model_delay` desplaza
todos los filtros hacia delante para conservarla en vez de recortarla. Es común a
todos los filtros de un diseño y el XTC solo depende de los retardos *entre*
ellos, así que cuesta latencia y nada más.

Medido (pipeline completo, 170 µs, 48 kHz, media 4–16 kHz):

| `model_delay` | suelo alcanzado | latencia |
|---|---|---|
| 0 | −62,5 dB | 0 |
| 32 | −79,3 dB | 0,67 ms |
| **64 (por defecto)** | **−85,7 dB** | **1,33 ms** |
| 256 | −100,6 dB | 5,33 ms |
| 1024 | −108,8 dB | 21,3 ms |

La asíntota de −108,8 dB es el truncado a 4096 taps del propio filtro. 64 deja
~23 dB de margen sobre ese suelo, que es de sobra.

## 3. Superficie de configuración

Desactivado por defecto en todas partes. Un diseño existente sigue produciendo
exactamente los mismos coeficientes.

| Dónde | Cómo |
|---|---|
| TOML (las cuatro herramientas) | `frac_delay = true` / `model_delay = 64`, de nivel superior |
| `natambio-xtc-filters` | `-F` y `-M N` |
| `xtc_filters.py` | `-F` y `-M N` |
| `natambio-xtc-filters-asym`, `xtc_filters_asym.py` | solo TOML, como el resto de sus parámetros |
| XML del motor | `<frac_delay>` y `<model_delay>` en `<xtc>` y `<xtc_asym>` |

Las dos claves TOML van al nivel superior, no dentro de `[xtc]`/`[left]`/
`[right]`: describen el diseño entero, no la trayectoria de un altavoz, y es lo
que mantiene esos tres bloques intercambiables.

`<frac_delay>` es el único booleano del esquema XML, así que tiene parser propio
(`parse_bool_tag()` en `naconf.cpp`) en vez de un `strtol` pelado: escribir
`<frac_delay>true</frac_delay>` y que se leyera como 0 es justo el malentendido
silencioso que este fichero evita en todo lo demás. Acepta `1/0`, `true/false`,
`yes/no`, `on/off`; cualquier otra cosa es error de parseo.

Los nombres de salida ganan `_frac` (simétrico sin prefijo) o el prefijo
asimétrico por defecto pasa a `XTC_asym_frac`, para que los dos diseños no puedan
pisarse.

## 4. Verificación

Todo esto está en `make check` (`lib/test_xtc_asym.c`) salvo donde se indique.

| Comprobación | Resultado |
|---|---|
| Camino entero C (sym + asym) vs HEAD, WAV a WAV | **0,000e+00 — bit-idéntico** |
| Camino entero Python vs HEAD, 4 configuraciones | **0,000e+00 — bit-idéntico** |
| `get_xtc_frac` vs `get_xtc` con ITD entero (250 µs = 12,000 muestras) | −309 dB relativo |
| Igual, asimétrico | −308 dB relativo |
| `process_asym` frac vs `process` frac, ambos lados iguales | 2,1e-9 relativo (la tolerancia existente es 1e-7) |
| `cross_left` vs `cross_right` con lados iguales | 0,0 exacto |
| Frac vs entero a 180 µs (8,64 muestras): **debe** diferir | 0,35 relativo |
| C vs Python en el camino fraccionario | ~1e-6, el mismo ruido FFTW-vs-numpy que ya tenía el camino entero |
| Curva de ITD entero vs la forma cerrada `a·2sin(πf·dt)`, 2–16 kHz | desviación máxima **0,35 dB** |

La última es la que valida la teoría, y está en
`tools/python_xtc_filters/compare_frac_delay.py`, que mide los dos diseños contra
la planta que la propia recursión modela,
`C(f) = a·A(f)·exp(-j2πf·itd_exacto)`.

### Ganancia medida (pipeline completo, 170 µs, 48 kHz)

| banda | sin filtro | ITD entero | ITD fraccionario | ganancia |
|---|---|---|---|---|
| 0,25–1 kHz | −8,7 dB | −46,2 | −88,6 | 42,4 dB |
| 1–2 kHz | −10,0 dB | −40,6 | −88,0 | 47,4 dB |
| 2–4 kHz | −11,4 dB | −35,3 | −87,3 | 52,0 dB |
| 4–8 kHz | −13,0 dB | −31,2 | −87,5 | 56,3 dB |
| 8–16 kHz | −14,9 dB | −26,9 | −84,5 | 57,6 dB |

Un barrido de 100–260 µs da 57–61 dB de forma consistente, **salvo a 250 µs**,
donde el ITD son 12,000 muestras exactas, no hay redondeo que corregir y los dos
caminos coinciden (−108,6 vs −108,7 dB). Ese punto es el control negativo: la
ganancia solo aparece cuando hay algo que ganar.

**Qué NO dicen estos números.** Son de auto-consistencia: miden el filtro contra
la planta para la que fue diseñado, es decir cuánta de la cancelación que el
diseño pretende sobrevive a la implementación. No son una afirmación sobre una
sala real, donde el desajuste de HRTF y el movimiento de cabeza limitan el XTC a
−15…−25 dB. Lo que cambia es que el redondeo del ITD, que a 8–16 kHz estaba en
−27 dB —el mismo orden que los límites físicos—, deja de competir con ellos.

## 5. Coste

`get_xtc_freq` tarda ~75 ms frente a ~9 ms del camino entero: el marco de FFT es
16× mayor (131072 puntos, dimensionado para que la convolución lineal completa no
se solape). Irrelevante en un generador offline que corre una vez. Si alguna vez
importara, el marco puede bajar a 65536 sin pérdida: el término más profundo de
la escalera vale 2,8e-18, por debajo de la épsilon de doble precisión relativa a
la delta unitaria.

## 6. Pendiente

- `compare_frac_delay.py` solo mide el caso simétrico. El asimétrico se verifica
  por equivalencia con el simétrico (`make check`), no por medida directa contra
  una planta asimétrica.
- Sin exponer en el port VST3.

### Divergencia aparte, a comunicar al autor del port

`Source/DSP/NatAmbio/XtcFilterDesigner.cpp:266` usa `fftSize = n` en
`minimumPhase()`, sin el sobremuestreo cepstral ×8 que sí tienen `lib/dsp.c:55` y
el port Python. Es exactamente el error de ~0,2 dB documentado en
`lib/dsp.c:157-175`, amplificado a ~5 % a través de las 16 convoluciones
encadenadas. No tiene relación con el ITD, pero conviene arreglarlo antes de
comparar nada.
