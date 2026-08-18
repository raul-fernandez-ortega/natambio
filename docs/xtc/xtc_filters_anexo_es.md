# Anexo — Relación con la literatura

### Complemento a *Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio*

*Also available in: [English](xtc_filters_annex_en.md)*

---

## Propósito de este anexo

El texto principal desarrolla el modelo de forma autocontenida, partiendo del análisis iterativo
de las cancelaciones sucesivas y llegando a los filtros $F^{direct}$ y $F^{cross}$. Ese desarrollo
se hizo de manera independiente, y por eso el texto no se apoya en referencias externas para
avanzar.

Este anexo existe para identificar **qué partes de ese desarrollo coinciden con resultados ya
publicados, cuáles son adaptaciones y cuáles son propias**. No añade nada al modelo: sirve para que
quien quiera ir a las fuentes primarias sepa a cuáles ir y por qué.

Se ha escrito aparte precisamente para no interrumpir el hilo del texto principal, cuyo lector
objetivo es el aficionado avanzado que quiere entender y montar el sistema, no seguir una
discusión bibliográfica.

> **Nota sobre las citas.** Las citas textuales de la literatura se reproducen **en su idioma
> original** para evitar que una traducción propia pueda tomarse por el texto del autor.

---

## A.1 · La estructura recursiva y la serie de potencias

El desarrollo iterativo del texto principal —el altavoz izquierdo emite, el pulso contamina el
oído derecho, el altavoz derecho emite un antipulso, que a su vez contamina el oído izquierdo, y
así sucesivamente— **es un resultado conocido**, y la serie a la que conduce está publicada.

**Origen de la recursión.** Atal, Hill y Schroeder (1966) fueron los primeros en reconocer que la
solución del problema de cancelación de diafonía es intrínsecamente recursiva.

**La serie de potencias.** Kirkeby, Nelson y Hamada (1998), en su análisis del *stereo dipole* bajo
condiciones de campo libre, resuelven el sistema y expanden el resultado exactamente así. Su
ecuación (13):

```math
\frac{1}{1-z} = \sum_{n=0}^{\infty} z^{n}, \qquad |z| < 1
```

y su ecuación (15), en el dominio del tiempo:

```math
\begin{bmatrix} v_1(t) \\ v_2(t) \end{bmatrix}
= r_1 \begin{bmatrix} -g_c\, d(t-\tau_c) \\[2pt] d(t) \end{bmatrix}
\ast \sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

Un tren de pulsos positivos en un altavoz y negativos en el otro, en múltiplos pares e impares del
retardo. **Es, término a término, el par de filtros del texto principal:**

```math
F^{direct} = \delta + \sum_{i=1}^{N} G^{2i}
\quad \Longleftrightarrow \quad
\sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

```math
F^{cross} = -\sum_{i=1}^{N} G^{2i-1}
\quad \Longleftrightarrow \quad
-g_c\, d(t-\tau_c) \ast \sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

Su interpretación física (§2.1.1 de su artículo) coincide además, frase por frase, con el
desarrollo iterativo de este documento.

**Consecuencia para la lectura del texto principal:** las ecuaciones de $F^{direct}$ y $F^{cross}$
no son una aportación original de NatAmbio. Lo que sí es propio es lo que se hace con ellas —ver
A.6, A.7 y A.11—.

**La condición de convergencia** $|G| < 1$ es la misma que su $|z| < 1$ de la ecuación (13).

---

## A.2 · Las definiciones de G y del retardo

**La misma definición.** Sus ecuaciones (1) y (2) definen

```math
g_c = \frac{r_1}{r_2}, \qquad \tau_c = \frac{r_2 - r_1}{c_0}
```

siendo $r_1$ el camino directo y $r_2$ el cruzado.

La $G = H_{cross}/H_{direct}$ del texto principal es la **forma general** de esa misma razón:
cociente entre la función de transferencia cruzada y la directa. Su $g_c$ es esa razón **evaluada
bajo condiciones de campo libre**, donde las funciones de transferencia son monopolos con caída
$1/r$ y por tanto el cociente se reduce a la razón de distancias.

**La diferencia sustancial está en cómo se puebla.** Ellos usan un modelo de campo libre con dos
fuentes puntuales y dos micrófonos, **sin cabeza**: la atenuación cruzada procede sólo de la
dispersión esférica sobre distancias desiguales. Con la geometría de este documento eso daría

```math
\Delta r \approx \Delta M \sin\Theta = 0.18 \cdot \sin 20^{\circ} = 61.6\ \text{mm}
```

```math
g_c = \frac{1.969}{2.031} = 0.970
\quad \Longrightarrow \quad
\text{ILD} \approx 0.27\ \text{dB}
```

frente a los **10 dB** que emplea NatAmbio, obtenidos de la sombra acústica de la cabeza a partir
de bases HRTF públicas. Un factor de unas 36 veces en amplitud.

Los dos modelos no se contradicen: miden cosas distintas. Pero la diferencia tiene una consecuencia
metodológica directa, que se recoge en A.8.

**Aviso de convención angular.** Su $\theta$ es el ángulo **total** entre altavoces; la $\Theta$ de
este documento es la **mitad**. Los 20° de aquí son los 40° de allí. Reconciliadas las
convenciones, su ecuación (17),

```math
\tau \approx \frac{\Delta M}{c_0}\,\sin\!\left(\frac{\theta}{2}\right)
\quad \Longrightarrow \quad
\tau(\theta = 40^{\circ}) = 179.5\ \mu\text{s}
```

coincide con los **180 µs** empleados aquí dentro del 0.3 %. Es esperable: el ITD es geometría, y
ambos caminos —el suyo desde distancias, el de este documento desde modelos HRTF— convergen al
mismo valor.

---

## A.3 · Una tecnología de sesenta años, ausente del audio comercial

La revisión anterior deja a la vista algo que merece comentario, porque explica el tono con que
está escrito el texto principal.

**La cronología.** El principio se patenta en **1966** (Atal, Hill y Schroeder). Se demuestra
experimentalmente con altavoces en cámara anecoica en **1971** (Damaske). Se mide su efecto y se
ensaya la alternativa de la barrera física en **1986** (Bock y Keele). La teoría madura entre 1989
y 1998: estructura suma/diferencia (Cooper y Bauck), filtros inversos adaptativos (Nelson, Hamada y
Elliott), *stereo dipole* y deconvolución regularizada (Kirkeby, Nelson, Hamada y
Orduña-Bustamante). Llega al ámbito doméstico con Ambiophonics a partir de **2001** y con los
filtros BACCH de Choueiri hacia **2008**.

Y a partir de ahí, el desarrollo público prácticamente se detiene.

**La explicación computacional sólo cubre la primera mitad.** Es cierto que la implementación
analógica de 1966 era limitada y que la convolución con filtros FIR largos no fue viable en tiempo
real hasta bien entrados los años noventa. Pero una convolución estéreo de 4096 coeficientes es
trivial para cualquier procesador desde hace veinte años. **La tecnología se abarató justo cuando
la industria dejó de perseguirla**, de modo que la falta de capacidad de cálculo no explica la
ausencia posterior.

**Motivos plausibles**, enunciados como tales y sin pretensión de tesis:

- **El punto de escucha.** Un XTC exige posición fija y simetría razonable. El audio de consumo se
  movió en la dirección contraria: varios oyentes, posiciones informales, barras de sonido.
- **La individualidad de la HRTF.** Un filtro genérico es siempre un compromiso, y la respuesta de
  la industria a ese problema fue el auricular con HRTF personalizada, no el altavoz.
- **La coloración.** Es la crítica histórica al XTC y la razón de ser de la regularización. Un
  procesado que mejora el espacio a costa del timbre es difícil de vender.
- **El auricular resuelve el problema por construcción.** No hay diafonía que cancelar, así que el
  esfuerzo en reproducción binaural se fue hacia allí.
- **Y en altavoces, la industria eligió la arquitectura opuesta**: más canales (5.1, 7.1, objetos)
  en lugar de mejor control de dos.

**Dónde sobrevive.** Decir que ha desaparecido es excesivo: subsiste en nichos comerciales
—los filtros BACCH—, en audio de automoción, en formas degeneradas dentro de los procesadores de
"ensanchamiento estéreo", y sobre todo en la práctica de aficionados. Pero es un hecho que la
investigación publicada sobre XTC es, en su mayor parte, anterior a 2010.

Que un principio demostrado hace medio siglo, hoy computacionalmente barato y con efecto
perceptual grande y fácil de comprobar, siga siendo territorio de aficionado, es una anomalía. Y
es, en buena medida, la razón de que este proyecto exista y se publique.

---

## A.4 · La frecuencia de *ringing*

Su ecuación (18) introduce un concepto que este documento maneja de forma implícita al hablar del
rizado en peine del filtro directo, pero que no llega a nombrar:

```math
f_0 \approx \frac{c_0}{\Delta M \cdot \theta}
\qquad\qquad
\text{regla práctica:}\quad f_0 \approx \frac{100\ \text{kHz}}{\theta\,[^{\circ}]}
```

Es la periodicidad espectral del tren de pulsos, equivalente a

```math
f_0 = \frac{1}{2\cdot\text{ITD}}
```

Con la geometría de este documento ($\theta = 40^{\circ}$ totales, ITD = 180 µs) resulta
$f_0 \approx 2.8$ kHz.

**Ése es exactamente el rizado que el texto principal mantiene dentro de la banda de ±2 dB.** El
término y la fórmula son adoptables tal cual, y nombran una magnitud que ya se está controlando.

**Dos palancas distintas sobre la misma coloración.** Su conclusión es que una apertura total de
10° es buena elección porque **sube $f_0$ por encima de 10 kHz**, sacando el rizado de la banda
audible, al precio de exigir un realce mayor en graves. NatAmbio trabaja a 40° totales, con $f_0$
de lleno en la banda audible, y ataca el problema por el otro lado: **reduciendo $|G|$** —el
desplazamiento del ILD descrito en A.7—, lo que baja la *amplitud* del rizado en lugar de sacar su
*frecuencia* de la banda.

Son palancas ortogonales y, en principio, combinables.

---

## A.5 · Graves: mal condicionamiento y limitación

La limitación del filtro cruzado por debajo de 200 Hz con caída de 6 dB/octava que describe el
texto principal responde a un problema documentado. Kirkeby, Nelson y Hamada lo enuncian así:

> *At low frequencies, the crosstalk cancellation problem is ill-conditioned. Consequently, each of
> the filters in the crosstalk cancellation network is likely to boost low frequencies by 30 dB or
> more.*
>
> — Kirkeby, Nelson y Hamada (1998), §3

Y en sus conclusiones señalan que reducir la apertura entre altavoces agrava ese realce, lo que
constituye el límite práctico de la estrategia del *stereo dipole*.

**La diferencia está en dónde se aplica el remedio.** Ellos lo tratan del lado de la **inversión**,
mediante regularización por mínimos cuadrados (Kirkeby, Nelson, Hamada y Orduña-Bustamante, 1998).
NatAmbio lo trata del lado del **modelo**: limitando espectralmente la propia función $G$ antes de
que entre en la serie. Es una solución más simple y, para este caso, suficiente.

---

## A.6 · Fase no mínima con HRTF reales

Éste es el punto en que el desarrollo de NatAmbio responde a una dificultad que la literatura deja
planteada:

> *When the free-field transfer functions are replaced by more realistic head-related transfer
> functions (HRTFs), it becomes necessary to consider the problem of inverting an ill-conditioned
> system that contains non-minimum-phase components.*
>
> — Kirkeby, Nelson y Hamada (1998), §3

A partir de ahí remiten a sus métodos de diseño de filtros FIR, abandonando la serie.

**La aportación de este documento es no abandonarla.** La serie se conserva y se puebla con HRTF,
esquivando la fase no mínima mediante una decisión de modelado: en lugar de HRTF medidas se emplea
un **modelo paramétrico monótono y de fase mínima** del espectro de ILD, deliberadamente carente
de picos y valles, precisamente porque la posición de éstos varía mucho con la anatomía individual.

Esa decisión hace además que el filtro no introduzca retardo de grupo apreciable, algo que los
métodos de deconvolución de la literatura sí hacen: introducen un retardo de modelado del orden de
media longitud de filtro.

---

## A.7 · El desplazamiento del ILD como parámetro de regularización

El texto principal reporta un ajuste empírico: fijar el ILD unos **4 dB por encima** del valor
natural de las HRTF da mejor equilibrio, porque produce un filtro cruzado más suave y reduce el
rizado en peine, cambiando algo de imagen espacial por menos coloración.

Vale la pena hacer explícito que **eso es funcionalmente un parámetro de regularización**. En el
marco de Kirkeby, Nelson, Hamada y Orduña-Bustamante (1998), la regularización de Tikhonov
administra exactamente ese compromiso: profundidad de cancelación frente a esfuerzo de filtro y
coloración. El mando es el mismo; la ruta para llegar a él es distinta —aquí, escucha; allí, un
criterio de optimización—.

Reconocerlo no resta valor al ajuste: lo sitúa. Y sugiere que el valor óptimo podría, en
principio, derivarse de un criterio en lugar de ajustarse a oído, cosa que en este trabajo no se
ha intentado.

---

## A.8 · Truncamiento: N = 3–4 en el texto, 16 iteraciones en el código

El texto principal justifica $N = 3\text{–}4$ para un ILD de 10 dB: cada incremento de $i$ reduce
el término unos 20 dB, de modo que el cuarto ya está del orden de −70 dB.

La implementación, sin embargo, recorre **16 iteraciones**. No es una discrepancia sino una
decisión de robustez: el generador de filtros no sabe *a priori* qué ILD va a configurar el
usuario, y el número de términos necesario depende de él. Como el término $i$ del filtro directo
decae con $`2i\cdot\text{ILD}_{dB}`$, el número de iteraciones requerido para llegar a un suelo de
−80 dB es

```math
i \approx \frac{40}{\text{ILD}_{dB}}
```

| ILD configurado | iteraciones necesarias |
|---|---|
| 20 dB | 2 |
| 10 dB | 4 |
| 5 dB | 8 |
| **2.5 dB** | **16** |

> **Las 16 iteraciones cubren cualquier ILD por encima de unos 2.5 dB dejando el último término
> retenido por debajo de −80 dB.** Con el ajuste habitual de 10 dB los términos por encima del
> cuarto son irrelevantes, pero la escalera se calcula completa por si la configuración baja.

Conviene señalar un límite adicional: la escalera empieza en el retardo
$(\text{NSTEPS}-1)\cdot\text{ITD}$, de modo que la longitud de filtro debe ser mayor que ese valor
o los términos de orden alto se truncan por longitud antes que por nivel. El generador avisa
cuando eso ocurre.

---

## A.9 · La estructura suma/diferencia

La formulación matricial del texto principal —la planta simétrica $`\mathbf{H} = H_{direct}\,\mathbf{C}_G`$
y su inversa— tiene una propiedad que no se desarrolla allí y que conviene conocer: la matriz
$\mathbf{C}_G$ y su inversa **comparten los autovectores**, con autovalores recíprocos:

```math
\mathbf{C}_G = \begin{bmatrix} 1 & G \\ G & 1 \end{bmatrix}
\qquad
\mathbf{u}_1 = \begin{bmatrix} 1 \\ 1 \end{bmatrix},\;
\mathbf{u}_2 = \begin{bmatrix} 1 \\ -1 \end{bmatrix}
\qquad
\lambda_{1,2} = 1 \pm G
```

En consecuencia, el diseño de un cancelador simétrico puede plantearse como **dos filtros
escalares independientes**

```math
\frac{1}{1+G} \qquad \text{y} \qquad \frac{1}{1-G}
```

en lugar de como la inversión de una matriz $2\times 2$. Es la estructura conocida como *shuffler*,
cuya aplicación al problema transaural desarrollan Cooper y Bauck (1989), y cuyo origen se remonta
al procesado suma/diferencia de Blumlein (1931).

Esta lectura explica de forma inmediata por qué todo el problema de graves de A.5 vive en un solo
escalar: es el autovalor $1-G$, que tiende a cero cuando $|G| \to 1$.

Y tiene una consecuencia que enlaza con A.10: **el signo de la correlación intercanal de una
componente determina cuál de los dos autovalores gobierna su paso por el sistema.** Para una
componente cuyos canales guardan la relación $r = \rho\, l$, la descomposición en la base propia da

```math
\begin{bmatrix} 1 \\ \rho \end{bmatrix}
= \underbrace{\frac{1+\rho}{2}}_{\text{peso simétrico}} \begin{bmatrix} 1 \\ 1 \end{bmatrix}
+ \underbrace{\frac{1-\rho}{2}}_{\text{peso antisimétrico}} \begin{bmatrix} 1 \\ -1 \end{bmatrix}
```

de modo que con $\rho > 0$ domina el peso simétrico y con $\rho < 0$ el antisimétrico.

---

## A.10 · Ambiente sintético frente a ambiente extraído

Éste es el punto en que NatAmbio se separa de la tradición de la que procede, y conviene decirlo
con claridad porque la arquitectura es la misma.

### Lo que propone Ambiophonics

Glasgal (2001) describe la cadena ambiofónica completa: reproductor → corrección de sala y altavoz
→ cancelador de diafonía → ambiopolo frontal, más un camino ambiental hacia los altavoces de
surround. Comparada con la figura de arquitectura del sistema NatAmbio, **es la misma cadena**.

La diferencia está en cómo se genera ese camino ambiental. Ambiophonics lo produce mediante
**convolución del sonido directo con la respuesta impulsional de una sala real**, medida antes o
después de la sesión de grabación, o tomada de una biblioteca de grandes auditorios. Es el
dispositivo que Glasgal llama *Ambiovolver*.

Sus argumentos para hacerlo así están razonados y son sólidos dentro de su marco: no es necesario
capturar la respuesta de la sala una y otra vez para cada obra; el número y la posición de los
altavoces de surround dejan de ser críticos; el ingeniero de grabación se libra del compromiso
entre perspectiva de las tomas principales y captación de ambiente; y el oyente puede incluso
elegir en qué sala quiere escuchar.

**El ambiente resultante, por tanto, no estaba en la grabación.** Procede de la respuesta
impulsional de otra sala, y se sintetiza en reproducción.

### Lo que hace NAE

NatAmbio parte de la restricción opuesta: **no crear información espacial que no esté en la
grabación**. NAE no dispone de biblioteca de salas, no convoluciona con respuestas impulsionales
ajenas y no genera reverberación. Descompone la señal estéreo en dos componentes ortogonales y
encamina la secundaria al dipolo trasero.

En ese esquema los papeles quedan repartidos así:

> **NAE es un realzador del ambiente natural que la grabación ya contiene. El XTC es el proyector
> que lo lleva a una posición virtual.**

Ninguno de los dos crea el ambiente: uno lo separa y el otro lo sitúa.

### El mismo eje, dos veces

Lo notable es que ésta es **exactamente la misma divergencia** que separa a NatAmbio de la
literatura de *primary-ambient extraction*, donde el ambiente se obtiene decorrelando —es decir,
sintetizando material que no estaba— para repartirlo entre varios altavoces.

| enfoque | origen del ambiente |
|---|---|
| Ambiophonics | lo **crea** por convolución con la RI de una sala real |
| Literatura PAE | lo **crea** por decorrelación artificial |
| **NatAmbio** | **no lo crea**: extrae el que ya está y lo proyecta |

La restricción de diseño es, por tanto, el **único eje distintivo del proyecto**, y opera en las
dos direcciones. Frente a la literatura PAE podría leerse como una diferencia de destino —ellos
alimentan un array, NatAmbio un dipolo—; frente a Ambiophonics, con la misma arquitectura, el
mismo objetivo doméstico y el mismo cancelador, es una diferencia de principio.

### El compromiso, dicho sin adornos

La ventaja de Ambiophonics es real y conviene reconocerla: **su ambiente siempre está disponible y
siempre es bueno**, porque procede de una sala elegida. El de NatAmbio es el que la grabación
contenga, con toda su variabilidad —y de una grabación monofónica no obtiene ninguno, como el
propio texto principal señala.

Lo que se gana a cambio es que el resultado guarda una relación comprobable con el material
original: la escena reproducida procede de la información espacial que el ingeniero de grabación
capturó, no de una decisión de reproducción.

### Lo que queda pendiente

La conexión entre NAE y el resto de la cadena descansa sobre una relación que **no está analizada
de forma robusta**: la que existe entre el grado de correlación de una componente y su
localización virtual bajo cancelación de diafonía.

Lo que se sabe hoy, y está recogido en la documentación de NAE:

- Los dos extremos son estructurales: una señal $[1,\,1]$ con desnivel nulo se percibe máximamente
  localizada y centrada, y una $[1,\,-1]$ con desnivel nulo, máximamente deslocalizada.
- Entre ambos, la lateralidad **no es monótona**: presenta un máximo con contenido anticorrelado y
  un desnivel entre canales del orden de 8 a 9 dB.
- La posición de ese máximo no se desplaza al variar el ILD ni el ITD del filtro, dentro de la
  resolución de la medida.

Lo que **no** se sabe:

- Por qué los máximos perceptuales caen donde caen. Hay hipótesis que encajan —relacionadas con la
  banda en que el filtro es neutro en magnitud y con la saturación de la lateralización por
  nivel— pero ninguna está comprobada.
- La cuantificación en grados de la apertura alcanzada en cada punto.
- Si las observaciones generalizan más allá de un oyente y una sala.

Es la pieza que conectaría de forma cuantitativa la descomposición de NAE con la cadena
XTC → DRC, y a día de hoy está abierta. Todas las medidas citadas se han obtenido con
herramientas incluidas en el repositorio público, de modo que son reproducibles por quien quiera
contrastarlas, ampliarlas o refutarlas.

---

## A.11 · Qué es propio de NatAmbio

Tras la comparación anterior, lo que no se encuentra en la literatura consultada:

1. **La aplicación del espectro de ILD una vez por ida y vuelta** ($S^{i}$ en lugar de
   $S^{2i-1}$). Es una decisión de modelado que sólo puede plantearse si existe una serie
   explícita, y no tiene equivalente en las formulaciones por inversión directa.
2. **La separación XTC / DRC enunciada como principio arquitectónico.** En un modelo de campo libre
   la cuestión no se plantea, porque $H_{direct}$ es trivial —un retardo y una caída $1/r$—; sólo
   aparece cuando $H_{direct}$ es una respuesta real medida, con ceros profundos, componentes de
   fase no mínima y dependencia de la posición.
3. **El modelo paramétrico monótono y de fase mínima del espectro de ILD**, ajustado sobre el
   promedio de cinco bases HRTF públicas, y su justificación explícita: evitar picos y valles cuya
   posición depende de la anatomía individual.
4. **Conservar la serie con HRTF**, que es la vía que la literatura declara difícil y abandona
   (A.6).
5. **La extracción del ambiente en lugar de su síntesis** (A.10), que separa a NatAmbio tanto de
   Ambiophonics como de la literatura PAE.

---

## A.12 · Requisitos prácticos recogidos de la literatura

Kirkeby, Nelson y Hamada establecen una condición sobre el hardware que merece figurar entre los
requisitos de montaje del sistema:

> *It is very important that the two loudspeakers have almost identical frequency responses (not
> just their amplitude responses, but also their phase responses must be the same). As a rule of
> thumb, "pair matching" to within ±0.5 dB in amplitude and ±5° in phase is more than sufficient
> to ensure accurate and symmetric imaging.*
>
> — Kirkeby, Nelson y Hamada (1998), §3

Es una condición exigente. La etapa de DRC de NatAmbio la aborda parcialmente —corrige la
respuesta de cada camino— pero conviene señalar que el emparejamiento de fase entre cajas es un
requisito previo que la ecualización no garantiza por sí sola.

---

## A.13 · Referencias primarias

Las siguientes se añaden a las ya citadas en el texto principal.

**Cancelación de diafonía: origen y formulación**

15. Atal, B. S., Hill, M. y Schroeder, M. R. (1966). *Apparent Sound Source Translator*.
    Patente EE. UU. 3.236.949. — Origen del reconocimiento de la naturaleza recursiva de la
    solución.
16. Kirkeby, O., Nelson, P. A. y Hamada, H. (1998). The "Stereo Dipole" — A Virtual Source Imaging
    System Using Two Closely Spaced Loudspeakers. *J. Audio Eng. Soc.* 46(5), 387–395. — Serie de
    potencias (ecs. 13–15), definiciones de $g_c$ y $\tau_c$ (ecs. 1–2), frecuencia de *ringing*
    (ec. 18), y planteamiento del problema de las HRTF reales (§3).
17. Cooper, D. H. y Bauck, J. L. (1989). Prospects for Transaural Recording. *J. Audio Eng. Soc.*
    37(1/2), 3–19. — Estructura suma/diferencia (*shuffler*) aplicada al problema transaural.
18. Bauck, J. y Cooper, D. H. (1996). Generalized Transaural Stereo and Applications.
    *J. Audio Eng. Soc.* 44(9), 683–705.

**Regularización e inversión**

19. Kirkeby, O., Nelson, P. A., Hamada, H. y Orduña-Bustamante, F. (1998). Fast Deconvolution of
    Multichannel Systems Using Regularization. *IEEE Trans. Speech and Audio Processing* 6(2),
    189–194. — Marco en el que el desplazamiento del ILD de A.7 encuentra su equivalente formal.
20. Nelson, P. A., Hamada, H. y Elliott, S. J. (1992). Adaptive Inverse Filters for Stereophonic
    Sound Reproduction. *IEEE Trans. Signal Processing* 40(7), 1621–1632.

**Validación experimental de la cancelación de diafonía**

21. Damaske, P. (1971). Head-Related Two-Channel Stereophony with Loudspeaker Reproduction.
    *J. Acoust. Soc. Am.* 50(4B), 1109–1115. — Demostración de la capacidad binaural de la
    cancelación de diafonía con altavoces en cámara anecoica.
22. Bock, T. M. y Keele, D. B. Jr. (1986). *The Effects of Interaural Crosstalk on Stereo
    Reproduction and Minimizing Interaural Crosstalk in Nearfield Monitoring by the Use of a
    Physical Barrier*, partes 1 y 2. AES 81st Convention, preprint 2420. — Versión medida del
    experimento de la barrera física descrito en la introducción del texto principal. Citada
    también por Glasgal para el mismo propósito.
23. Takeuchi, T., Kirkeby, O. y Nelson, P. A. (2007). The binaural performance of a cross-talk
    cancellation system with matched or mismatched setup and playback acoustics. *J. Acoust. Soc.
    Am.* 121(2), 1056. — Comportamiento binaural medido de filtros XTC digitales, incluido el caso
    de desajuste geométrico.

**Ambiente sintético y arquitectura ambiofónica**

24. Glasgal, R. (2001). *The Ambiophone — Derivation of a Recording Methodology Optimized for
    Ambiophonic Reproduction*. AES 19th International Conference, Schloss Elmau. — Cadena
    ambiofónica completa y generación del ambiente por convolución con respuestas impulsionales de
    salas reales (*Ambiovolver*). Es el punto de contraste de A.10.

**Coloración del estéreo sin cancelar**

25. Vickers, E. *Fixing the Phantom Center: Diffusing Acoustical Crosstalk*. AES. — Cuantifica los
    ILD e ITD incorrectos que la diafonía introduce para la imagen central, el filtrado en peine
    del campo directo y el hoyo espectral en torno a 2 kHz.

**Procesado suma/diferencia**

26. Blumlein, A. D. (1931). Patente británica 394.325. — Origen del procesado suma/diferencia.

> **Nota.** Los datos bibliográficos de las referencias 17 a 26 proceden en parte de fuentes
> secundarias y conviene verificarlos contra la AES E-Library, IEEE Xplore o JASA antes de
> considerarlos definitivos.
