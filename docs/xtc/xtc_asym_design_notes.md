# Notas de diseño: XTC asimétrico (`<xtc_asym>`)

Notas de trabajo de la sesión del 2026-07-28. **Estado: implementado el 2026-07-29** en
`lib/xtc_asym.{c,h}`, `src/naconf.cpp` y la documentación asociada. Se conservan como registro
del razonamiento y de lo verificado sobre el código; §7 recoge lo que quedó hecho y lo que no.

Referencia matemática: [Aplicación de NatAmbio XTC en entornos no simétricos](xtc_no_simetrico_es.md).

---

## 1. Resumen del modelo

Con $\mathbf{H} = H_{ll}\,\mathbf{M}\,\mathbf{D}$, donde

```math
\mathbf{M} = \begin{bmatrix} 1 & G_{r} \\ G_{l} & 1 \end{bmatrix}, \qquad
\mathbf{D} = \operatorname{diag}(1,\ b), \qquad P = g_l \ast g_r \ast \bar{S}
```

los filtros son:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^i, \qquad
F^{cross}_l = - G_{r} \ast \sum_{i=1}^{N} P^{i-1}, \qquad
F^{cross}_r = - G_{l} \ast \sum_{i=1}^{N} P^{i-1}
```

y el filtrado completo es $\mathbf{F}_{XTC} = \mathbf{D}^{-1}\mathbf{M}^{-1}$ (el orden importa:
$\mathbf{D}^{-1}$ **premultiplica**, porque escala la fila del altavoz).

**Los dos filtros directos son el mismo**, ya que solo dependen de $P$, que es simétrico bajo
intercambio de canales. Al sacar $b$ fuera (ver §3.3) son literalmente el mismo array.

### Convenio del espectro ILD (verificado empíricamente, 2026-07-29)

Sondeando `get_xtc()` con `ild_filter` = delta desplazada $m$ muestras, los taps cruzados salen en
$8, 21, 34\dots$ con $d{=}5, m{=}3$, es decir en $(2i{-}1)d + i\,m$ y **no** en $(2i{-}1)(d+m)$.
Conclusión: **el código aplica el espectro ILD una sola vez por escalón** ($S^i$), mientras el
retardo y el ILD de banda ancha sí siguen la ley $(2i{-}1)$. La nota técnica especificaba
$G^{2i-1}$, o sea $S^{2i-1}$: divergían. Enmendadas las notas para documentar el convenio real,
que es el que está en producción y contra el que se ajustó $\alpha$ de oído.

Separando $G_x = g_x \ast S_x$ (banda ancha y forma espectral), el índice $i$ **es el mismo** en el
filtro directo y en los cruzados —numera el mismo escalón—, y el número de aplicaciones del
espectro es $i$ en ambos; lo único que difiere es la parte de banda ancha, $g^{2i}$ frente a
$g^{2i-1}$. Por eso el exponente espectral se puede fijar sin tocar la iteración.

$\bar{S}$, el espectro de la ida y vuelta, es la media de las log-magnitudes de $S_l$ y $S_r$.
Como el modelo es lineal en $\alpha\sin\Theta$, se obtiene evaluándolo con
$\bar{\kappa} = \tfrac{1}{2}(\alpha_l \sin\Theta_l + \alpha_r \sin\Theta_r)$, sin promediar
respuestas. Cada cruzado recibe además una aplicación de **su propio** $S_l$ o $S_r$ en el término
de primer orden, que es el dominante: es lo que los mantiene espectralmente distintos.

### Cadencia de la escalera con $\text{ITD}_l \neq \text{ITD}_r$

Con $\tau_l,\tau_r$ en muestras y $T = \tau_l + \tau_r$, todos los filtros comparten cadencia $T$
y solo cambia el desfase inicial:

| Filtro | Taps |
|---|---|
| $F^{direct}$ | $0,\ T,\ 2T,\ \dots,\ NT$ |
| $F^{cross}_l$ (alimenta L) | $\tau_r,\ \tau_r+T,\ \dots,\ \tau_r+(N{-}1)T$ |
| $F^{cross}_r$ (alimenta R) | $\tau_l,\ \tau_l+T,\ \dots,\ \tau_l+(N{-}1)T$ |

- **El cruzado lleva el ITD del altavoz contrario** (`cross_l` va desfasado $\tau_r$). Sitio clásico
  para cruzar índices: ese pulso cancela la diafonía del altavoz *derecho* hacia el oído izquierdo.
- Los dos peines cruzados quedan en posiciones complementarias dentro de cada período,
  ya que $\tau_l + \tau_r = T$.
- La escalera **no se estira** por la asimetría: su extensión es $N(\tau_l+\tau_r) = 2N\bar\tau$,
  depende solo de la suma. La comprobación de longitud de [xtc.c:225](../../lib/xtc.c#L225) se
  reescribe en términos de $\tau_l + \tau_r$.
- Niveles: tap $i$ del directo a $-i(\text{ILD}_l+\text{ILD}_r)$ dB; tap $i$ de `cross_l` a
  $-[(i{-}1)(\text{ILD}_l+\text{ILD}_r) + \text{ILD}_r]$ dB (con $\text{ILD}_l$ en `cross_r`).

### Sobre el orden de los factores

La convolución es conmutativa y asociativa: **dentro de cada filtro el orden es irrelevante**.
Lo que no conmuta es el producto matricial, y al bajar a código eso no se traduce en orden sino
en cableado (qué $G$ va a qué cruzado, qué canal lleva el balance).

El orden sí importa en las operaciones **no lineales** que rodean a las convoluciones:

1. **Truncación.** `fft_convolve_truncate` recorta a `length` en cada vuelta
   ([xtc.c:154](../../lib/xtc.c#L154)); truncar no conmuta con convolucionar.
2. **Limitación espectral.** La rampa de 6 dB/oct bajo 200 Hz aplicada a $G$ dentro de la escalera
   queda elevada a la potencia $i$; aplicada al cruzado final, no. No es conmutatividad, es punto
   de aplicación.
3. **Normalizaciones.** Ahora hay dos espectros ILD y dos normalizaciones L2 independientes
   ([xtc.c:282-286](../../lib/xtc.c#L282-L286)); mantener la misma convención en ambas ramas
   (cada $h_{min}$ transporta solo forma; el nivel vive en el término `pow(10, att/20)`).

### Escalera por Horner

$S_N = \delta + P\ast(\delta + P\ast(\delta + \cdots))$, es decir:

```
S = δ
repetir N veces:  S = δ + p ⊛ S
```

Es lo que ya hace [xtc.c:140-167](../../lib/xtc.c#L140-L167), con el retardo explícito en el índice
del tap. En la versión asimétrica el retardo va dentro de $p = g_l \ast g_r$. Ventajas: un solo
buffer, y **el snapshot sale gratis** — la recurrencia produce $S_1, S_2, \dots, S_N$ en orden, así
que $S_{N-1}$ (el factor de los dos cruzados) es el acumulador *antes de la última vuelta*.

---

## 2. Decisión: función separada, no generalizar `get_xtc`

**`get_xtc_asymmetric` nuevo, `get_xtc` intacto.**

Razones:

- No es "la misma función con más parámetros": cambia la aridad de entrada (2 ITD, 2 ILD, 2
  espectros) y la de salida (3 filtros en vez de 2). Generalizar obliga al caso común a pasar
  buffers que no necesita, o a salidas opcionales con `NULL`, que es lo peor de ambos mundos.
- `get_xtc` es código endurecido y en producción ([xtc.c:106-123](../../lib/xtc.c#L106-L123));
  reescribir su bucle como Horner-sobre-$p$ le cambia la salida en los últimos bits e invalida
  cualquier WAV de referencia, y todo para dar servicio al caso raro.
- Los bucles son estructuralmente distintos: **el truco de una sola pasada compartida con un único
  `ild_filter` no sobrevive a $G_l \neq G_r$**, porque el término directo se diferencia de cada
  cruzado por un factor distinto:

```math
P^i = G_l^i G_r^i, \qquad G_r P^{i-1} = G_l^{i-1} G_r^{i}, \qquad G_l P^{i-1} = G_l^{i} G_r^{i-1}
```

- **Dos derivaciones independientes son un activo de test:** `get_xtc_asymmetric` con parámetros
  iguales debe reproducir `get_xtc` dentro de tolerancia. Ese cross-check se pierde al fusionarlas.

Firma final (el núcleo devuelve $\mathbf{M}^{-1}$ puro, **sin balance**). En la implementación se
llamó `get_xtc_asym`, por coherencia con el nombre del fichero y del tag:

```c
/* núcleo simétrico: intacto */
int get_xtc(int length, double attenuation, int delay,
            const double *ild_filter, double *direct_out, double *cross_out);

/* núcleo asimétrico: M⁻¹, sin balance */
int get_xtc_asym(int length,
                 double att_l, double att_r, int delay_l, int delay_r,
                 const double *ild_mean, const double *ild_l, const double *ild_r,
                 double *direct_out, double *cross_left_out, double *cross_right_out);
```

**Dónde sí compartir: un nivel más arriba.** Las ~50 líneas de `process()` que van de `firwin2` a
la normalización L2 ([xtc.c:234-286](../../lib/xtc.c#L234-L286)) son exactamente "construir un $G$".
Extraer a un helper estático:

```c
static int build_g(int azimuth_deg, double ild_alpha, int sample_rate,
                   int filter_len, double *g_out);
```

`process()` lo llama una vez, `process_asym()` dos. Cada sub-bloque `<left>`/`<right>` del XML
es exactamente una llamada a `build_g()`.

**Corregido en la implementación:** este plan no era viable. `xtc.c` está congelado porque lo
replican ports de terceros, así que no se pudo extraer nada de él. El helper vive en `xtc_asym.c`
como `build_ild_filter()` y la cadena `firwin2` → RMS → `minimum_phase` → RMS → L2 queda
**duplicada** entre ambos ficheros, con un comentario en cada uno obligando a mantenerlas en
paralelo. El test de equivalencia de `make check` es lo que impide que se separen sin avisar.

---

## 3. Decisión: configuración

### 3.1 El tag

`<xtc_asym>`, hermano de `<xtc>`. Sub-bloques `<left>`/`<right>` con el juego completo de
parámetros de cada lado — incluido `<ild_alpha>`, que va **por lado** (así cada sub-bloque mapea
1:1 con `build_g()`). Solo `<length>` es común, y tiene que serlo porque el convolver los encadena.

```xml
<xtc_asym>
  <left>  <itd_us>180</itd_us> <ild_db>10</ild_db> <ild_alpha>1.8</ild_alpha> <azimuth_deg>20</azimuth_deg> </left>
  <right> <itd_us>160</itd_us> <ild_db>9</ild_db>  <ild_alpha>1.8</ild_alpha> <azimuth_deg>17</azimuth_deg> </right>
  <length>4096</length>
  <direct_filter_name>XTC direct</direct_filter_name>
  <cross_left_filter_name>XTC cross L</cross_left_filter_name>
  <cross_right_filter_name>XTC cross R</cross_right_filter_name>
</xtc_asym>
```

**Tres coeffs, no cuatro:** al ir el balance fuera, los dos directos son el mismo filtro bit a bit,
así que se emite un único `<direct_filter_name>` (mismo nombre de tag que `<xtc>`). El bloque queda
casi idéntico al simétrico y el enrutado también.

### 3.2 Estructura en `naconf`

Al revés que en el nivel DSP: **reutilizar `struct xtc`** con un `bool asymmetric` y los campos por
lado, una sola `xtclist`, y que `build_xtc_coeffs()` ramifique solo en la llamada a `process()` y en
la emisión de coeffs. Lo que se duplicaría aquí es bookkeeping (colisiones, `malloc`,
`make_mem_coeff`, gestión de errores), no un bucle numérico endurecido. Sí conviene un
`parse_xtc_asym()` aparte, porque los tags son otros.

### 3.3 El balance va fuera de `<xtc_asym>`

No es un parámetro del tag. Se documenta para que el usuario lo ajuste con el `<gain>` de los dos
`<convol>` del canal que toque. El núcleo DSP no lo ve nunca — el código refleja así la estructura
de la nota: la escalera calcula $\mathbf{M}^{-1}$, el enrutado aplica $\mathbf{D}^{-1}$.

Es un ajuste manual sí o sí, viva el valor donde viva en el XML, así que sacarlo no añade trabajo
al usuario.

---

## 4. Verificado sobre el código actual

- **La costura es `make_mem_coeff()`** ([naconf.cpp:815](../../src/naconf.cpp#L815)): a partir de
  ahí un coeff generado en memoria es indistinguible de uno leído de fichero. `build_xtc_coeffs()`
  solo llama a `process()` y empuja a `coefslist`
  ([naconf.cpp:874-884](../../src/naconf.cpp#L874-L884)). **El convolver solo ve nombres.**
- **El enrutado no cambia.** [convol_drc_xtc.xml:7-10](../config_samples/convol_drc_xtc.xml#L7-L10)
  ya declara los cuatro caminos por separado; hoy dos comparten nombre de coeff, en asimétrico cada
  cruzado referencia el suyo. Cero cambios en `convchannel`, motor de convolución y resto de `naconf`.
- **El dispatch de tags** ([naconf.cpp:1260](../../src/naconf.cpp#L1260)) es una cadena `if/else`
  **sin DTD ni validación de esquema**: basta una rama nueva, no hay ningún otro sitio donde
  declarar el tag.
- **`<convol><gain>` existe y va en dB**, convertido con `FROM_DB`
  ([naconf.cpp:439-440](../../src/naconf.cpp#L439-L440)). El balance por configuración es un
  mecanismo que ya existe: cero código.

---

## 5. Trampas identificadas

- **Bug latente en la comprobación de nombres** ([naconf.cpp:849](../../src/naconf.cpp#L849)): solo
  consulta `find_coeff()`, que mira los coeffs ya insertados. Si un bloque declara el mismo nombre
  en `direct_filter_name` y `cross_filter_name`, ambos pasan y se insertan dos coeffs homónimos.
  **Ya existe hoy con dos nombres**; con tres es más probable. Comprobar también los nombres del
  propio bloque entre sí.
- **Redondeo de los ITD:** si se redondean $\tau_l$ y $\tau_r$ por separado,
  $\text{round}(\tau_l)+\text{round}(\tau_r)$ puede diferir en una muestra de
  $\text{round}(\tau_l+\tau_r)$, y la escalera deja de ser exactamente periódica si los índices se
  calculan analíticamente. Se evita construyendo por Horner sobre $p$, que ya lleva el retardo
  redondeado correcto.
- **Test de regresión con tolerancia, no bit-exacto:** el caso simétrico vía
  `get_xtc_asymmetric` con $g_l = g_r$ no dará bit-exacto (distinto número y agrupación de FFTs).
- Recalcular el aviso de `first_tap` ([xtc.c:225](../../lib/xtc.c#L225)) con el nuevo espaciado.

---

## 6. El balance no es cosmético

Comprobado: si el usuario no lo aplica, el filtro es $\mathbf{M}^{-1}$ en lugar de
$\mathbf{D}^{-1}\mathbf{M}^{-1}$, y

```math
\mathbf{H}\,\mathbf{M}^{-1} = \frac{H_{ll}}{1-P}\begin{bmatrix} 1-bP & G_r(b-1) \\ G_l(1-b) & b-P \end{bmatrix}
```

Los términos cruzados **no se anulan**: quedan proporcionales a $(b-1)$. Es coherente con que $b$
esté fuera del *bucle recursivo* (no afecta a la convergencia), pero fuera del bucle no significa
opcional. El techo de cancelación que impone un balance mal ajustado es $\approx 20\log_{10}|1-b|$:

| Error de balance | Techo de cancelación |
|---|---|
| 0.5 dB | ≈ −25 dB |
| 1 dB | ≈ −19 dB |
| 2 dB | ≈ −14 dB |
| 3 dB | ≈ −11 dB |
| 6 dB | ≈ −6 dB |

**Regla práctica: ajustar el balance a ~1 dB pone el techo en unos 20 dB de cancelación**, del orden
de lo que el propio modelo puede dar. Esa cifra convierte "ajusta a oído" en un objetivo con
tolerancia.

### Procedimiento a documentar

1. **Cuáles son los dos `<convol>`:** los que comparten `to_output`, con independencia del
   `from_input`. En [convol_drc_xtc.xml:9-10](../config_samples/convol_drc_xtc.xml#L9-L10) son
   "XTC direct DRC right" y "XTC cross DRC right": uno viene de `input_right` y otro de
   `input_left`, pero ambos alimentan `output_right`. Es $\mathbf{D}^{-1}$ escalando la fila del
   altavoz.
2. **Siempre atenuar, nunca amplificar.** El factor estricto es $1/b$ en el derecho, pero
   multiplicar toda la matriz por $b$ es equivalente y da $\operatorname{diag}(b,1)$: atenuar el
   izquierdo. Instrucción al usuario: `<gain>` **negativo** en los dos convol del canal que llega
   más fuerte, el otro a 0 dB. Nunca positivo, que solo gasta headroom.
3. **Cómo ajustarlo:** con una señal mono reproducida en estéreo, atenuar hasta que la imagen quede
   centrada. A partir de ahí se da por bueno.
4. Si el DRC de la cadena ya nivela ambos canales contra un objetivo común, $b \approx 1$ y el
   ajuste se queda en 0 dB. El caso está para cuando no es así.

---

## 7. Estado final

**Código — hecho**

- [x] Helper de construcción de $G$: `build_ild_filter()` en `lib/xtc_asym.c` (no extraído de
      `xtc.c`, ver §2).
- [x] `get_xtc_asym()` + `process_asym()` en [lib/xtc_asym.c](../../lib/xtc_asym.c) /
      [lib/xtc_asym.h](../../lib/xtc_asym.h).
- [x] Test de equivalencia con tolerancia: [lib/test_xtc_asym.c](../../lib/test_xtc_asym.c),
      integrado como `make check` en `lib/Makefile.am`. Equivalencia a ~1.9e-9 relativo (redondeo
      de las FFT); comprueba además que cada cruzado arranca con el ITD del lado contrario.
- [x] `struct xtc` + `bool asymmetric` y `struct xtc_side` en
      [src/structs.hpp](../../src/structs.hpp).
- [x] `parse_xtc_asym()` + `parse_xtc_side()` y rama nueva en el dispatch de `naconf.cpp`.
- [x] Rama en `build_xtc_coeffs()` (3 coeffs), unificada para ambos tags.
- [x] Comprobación de nombres duplicados dentro del propio bloque (el bug latente de §5).
- [x] `xtc_asym` en `lib/Makefile.am` y `src/Makefile.simple`; artefactos de `make check` en
      `.gitignore`; `build-aux/test-driver` añadido por automake (versionarlo, como el resto de
      `build-aux/`).

**Documentación — hecho**

- [x] Tag `<xtc_asym>` en [src/README.md](../../src/README.md) y
      [docs/README.CONFIG](../README.CONFIG), con la sección de balance completa.
- [x] [convol_drc_xtc_asym.xml](../config_samples/convol_drc_xtc_asym.xml) y su entrada en el
      índice de samples.
- [x] Nota técnica enlazada desde el índice ([README_es.md:86](../README_es.md#L86)).
- [x] Convenio $S^i$ documentado en las tres notas (principal es/en y asimétrica), ver §1.

**Pendiente**

- [x] Ajuste del balance ($\mathbf{D}^{-1}$ como ganancia de enrutado, procedimiento mono a
      estéreo, atenuar nunca amplificar) y techo de cancelación, añadidos a la nota técnica.
- [x] Protección en baja frecuencia (corte a 200 Hz, 6 dB/oct) documentada en la nota asimétrica,
      verificada sobre los filtros generados.
- [x] Versión inglesa de la nota asimétrica: [xtc_no_simetrico_en.md](xtc_no_simetrico_en.md),
      enlazada desde [docs/README.md](../README.md).
- [ ] Escucha real en un sistema asimétrico: nada de esto se ha validado de oído todavía.

**Huecos de documentación detectados de paso** (ajenos a este trabajo)

- [ ] `install.md` solo existe en inglés — es la única pieza sin pareja `_es`, y contiene el aviso
      de ganancias del primer arranque ([install.md:302-306](../install.md#L302-L306)).
- [ ] `**[Pendiente de obtener un ejemplo de output]**` en
      [como_medir_respuestas_impulsivas.md:280](../como_medir_respuestas_impulsivas.md#L280) y su
      gemelo en [how_to_measure_impulse_responses.md:231](../how_to_measure_impulse_responses.md#L231),
      justo en el párrafo del headroom.
- [x] Tres config samples preexistentes no parseaban por llevar `--` dentro de comentarios XML
      (`dual_system_nae_loudness_drc_xtc.xml`, `multi_out_drc_xtc_bypass.xml`,
      `nae_xtc_drc_vs_bypass.xml`). Corregido el 2026-07-29: separadores con `=` y la opción
      larga de `jack_snapshot` reformulada. Los 26 samples validan con `xmllint`.

**Ya verificado, no hace falta tocar:** los avisos sobre ganancias en sweeps
([como_medir_respuestas_impulsivas.md](../como_medir_respuestas_impulsivas.md): modo `CALIBRATE=1`,
`check_capture.py`, headroom de 10 dB, umbrales de clipping/nivel/SNR) y en el primer arranque
([install.md:302-306](../install.md#L302-L306)) están cubiertos.
