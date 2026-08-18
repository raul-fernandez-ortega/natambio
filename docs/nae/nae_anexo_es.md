# Anexo — NatAmbio en contexto

### Complemento a *NatAmbio Ambient Extractor (NAE)*: relación con la literatura de *primary-ambient extraction*

*Also available in: [English](nae_annex_en.md)*

**Autor:** Raúl Fernández Ortega
**Estado:** borrador de trabajo

---

## Propósito de este anexo

El artículo principal describe el algoritmo NAE de forma autocontenida. Este anexo lo sitúa
respecto a un cuerpo de literatura —la *primary-ambient extraction* (PAE)— que aborda un problema
formalmente idéntico, la separación de una señal estéreo en componente principal y componente
ambiental, y llega a una definición de "ambiente" **opuesta** a la que NatAmbio emplea.

Ese contraste no es anecdótico: explica por qué un lector procedente del procesado de audio
espacial encuentra extrañas algunas de las definiciones de este proyecto. El anexo existe para
responder a esa extrañeza con precisión, y para identificar qué es conocido, qué es adaptación y
qué es propio.

> **Sobre la procedencia de estas ideas.** NatAmbio nace de la línea Ambiophonics / PanAmbio
> (Glasgal, Miller) y de la práctica de la cancelación de diafonía. La literatura PAE se encontró
> **después** de que el algoritmo estuviera desarrollado e implementado, y se discute aquí como
> contexto y como comprobación, no como fundamento. Se cita lo que hay que citar, pero el orden de
> las cosas fue ése.

> **Sobre las citas.** Las citas textuales de la literatura se reproducen **en su idioma original**
> para evitar que una traducción propia pueda tomarse por el texto del autor.

> **Documento hermano.** El [anexo del artículo de filtros XTC](../xtc/xtc_filters_anexo_es.md)
> hace el mismo trabajo para la cancelación de diafonía. Los dos son mitades complementarias de un
> mismo argumento, y se remiten mutuamente donde corresponde.

---

## A.1 · Qué es esto, y cuál es la tesis

NatAmbio es un sistema de reproducción doméstico. No es un formato, ni un códec, ni un prototipo
de laboratorio. Se desarrolló en una vivienda particular, en un salón de uso general, con hardware
barato, y su objetivo es concreto y modesto:

> **Que las grabaciones estéreo comerciales —las de cualquier época, las que uno ya tiene— suenen
> mejor de lo que suenan en una cadena estéreo convencional.**

Todo lo que sigue está subordinado a eso. Cuando aparezca una decisión de diseño que parezca
arbitraria desde un punto de vista teórico, casi siempre la explicación es que resolvía un problema
concreto de escucha en un salón concreto.

### La tesis

Ninguna de las piezas de NatAmbio es nueva. La cancelación de diafonía se patenta en 1966. El
procesado suma/diferencia es de 1931. La PCA aplicada a señales estéreo lleva veinte años en la
literatura. La arquitectura de doble dipolo es de Robin Miller.

> **Lo nuevo, si hay algo nuevo, es el conjunto:** una descomposición conocida, sometida a una
> restricción concreta —no inventar información espacial— y colocada dentro de una arquitectura
> de reproducción concreta que es la que le da sentido.

---

## A.2 · De dónde viene: la diafonía es real

Hace años implementé una versión muy básica de RACE (*Recursive Ambiophonic Crosstalk
Elimination*) para probar Ambiophonics en casa. El resultado fue el descubrimiento experimental
de algo que había leído muchas veces sin que me dejara huella:

> **La diafonía interaural parece transparente y no lo es. Reduce muchísimo el potencial del
> estéreo.**

En una cadena estéreo convencional, cada oído recibe los dos altavoces. La señal que llega al
tímpano no es la del canal correspondiente: es una mezcla de ambos, filtrada por la cabeza y
dependiente de la frecuencia. Sus efectos documentados son pistas de localización incorrectas
—con el caso peor precisamente en la imagen centrada—, filtrado en peine severo en el campo
directo con el conocido hoyo espectral en torno a los 2 kHz, y una escena confinada entre los
altavoces [Vickers].

Que esto se puede corregir está demostrado desde hace medio siglo [Damaske, 1971], se ha medido
[Bock y Keele, 1986] y se ha verificado sobre filtros digitales, incluido el caso en que la
geometría de diseño no casa con la de escucha [Takeuchi, Kirkeby y Nelson, 2007].

Nada de esto es aportación de NatAmbio: es el suelo sobre el que se apoya. **La razón de ser de
todo el sistema es que la diafonía es un problema real y que corregirla libera algo que las
grabaciones ya contienen.**

> El desarrollo de este punto —cronología completa, por qué la tecnología está hoy ausente del
> audio comercial pese a ser barata, y el detalle de las referencias— está en la sección A.3 del
> [anexo de XTC](../xtc/xtc_filters_anexo_es.md).

---

## A.3 · La arquitectura: PanAmbio

NatAmbio adopta la arquitectura **PanAmbio** propuesta por Robin Miller: dos dipolos estéreo
Ambiophonics, uno frontal y otro trasero, cada uno con su propia cancelación de diafonía.

Un *dipolo estéreo* (o **ambiopolo**) es un par de altavoces muy juntos, típicamente entre 10° y
30° de apertura. Gracias a la cancelación de diafonía, un solo ambiopolo puede desplegar una
imagen de casi 180°; dos ambiopolos cubren los 360°. A esto se añade ecualización por **DRC**
(*Digital Room Correction*) de ambos dipolos y, si se usa, del subgrave, para el equilibrio tonal.

### Por qué pocos altavoces, y no muchos

Ésta es la divergencia arquitectónica de la que se derivan casi todas las demás:

> En un sistema multialtavoz, **añadir altavoces mejora**: cada uno aporta una dirección
> independiente y el campo resultante es más envolvente.
>
> En un sistema basado en cancelación de diafonía, **añadir altavoces empeora**: cada altavoz
> adicional introduce caminos acústicos hacia ambos oídos que el filtro de cancelación no modela
> y por tanto no puede corregir.

Un sistema XTC compra precisión y anchura a cambio de un compromiso: hay que conocer y controlar
los caminos de transmisión, y hay que aceptar un punto de escucha. Un sistema multialtavoz compra
robustez a cambio de renunciar al control fino de lo que llega a cada tímpano.

Ninguna de las dos opciones es mejor en abstracto. Pero son opuestas, y **casi todo el desacuerdo
entre NatAmbio y la literatura PAE se explica por esta única diferencia**, como se verá en A.9.

---

## A.4 · El problema que esto plantea

PanAmbio necesita cuatro canales: el par frontal y el par ambiental. La práctica totalidad de la
música grabada tiene dos.

De ahí sale el encargo que da origen a NAE:

> **Generar señales compatibles con PanAmbio a partir de grabaciones estéreo estándar, sin
> manipulación artificial ni de la localización ni del ambiente.**

Esa segunda parte es una restricción de diseño, y es el elemento que sostiene todo lo que viene
después. Significa, en concreto:

- No se añade reverberación.
- No se aplica decorrelación artificial.
- No se sintetiza ningún canal.
- Toda la información espacial reproducida procede exclusivamente de la señal estéreo original.

Formulado de otro modo: **el sistema puede redistribuir la información espacial que ya está en la
grabación, pero no puede crear información nueva.** De una grabación monofónica —dos canales
idénticos— NatAmbio no obtiene ambiente, y reproduce con foco total en el centro de la escena.
Eso no es una limitación que haya que disculpar: es la comprobación de que el sistema hace lo que
dice.

---

## A.5 · NAE: qué hace

### A.5.1 El algoritmo

NAE (*NatAmbio Ambient Extractor*) es una PCA de ventana deslizante sobre la covarianza estéreo,
en **banda completa** y en el **dominio del tiempo**, pensada para funcionar en tiempo real con un
coste computacional muy bajo.

Por cada bloque de proceso:

```math
\begin{aligned}
M &= L + R \\
S &= \beta\,(L - R)
\end{aligned}
```

Se acumula la covarianza $2\times 2$ de $(M, S)$ sobre una ventana deslizante de `covsteps`
bloques y se descompone en autovalores, obteniendo $\mathbf{u}_1$ y $\mathbf{u}_2$, ortogonales
por construcción. Las componentes son

```math
c_1 = \mathbf{u}_1^{\mathsf{T}}\begin{bmatrix} M \\ S \end{bmatrix}, \quad
c_2 = \mathbf{u}_2^{\mathsf{T}}\begin{bmatrix} M \\ S \end{bmatrix}
\qquad
C_1 = c_1\,\mathbf{u}_1, \quad C_2 = c_2\,\mathbf{u}_2
```

que se devuelven a L/R con solapamiento sobre `covsteps` análisis sucesivos, y la salida es

```math
\text{salida} = \alpha\,C_1 + \beta_{\text{amb}}\,C_2
```

$C_1$ y $C_2$ salen como **pares estéreo completos**, cada uno con su propio panorama, y se
encaminan a los dipolos con ganancias ajustables.

### A.5.2 Sobre el uso del plano M/S

Este documento usa coordenadas M/S en todo momento, y las figuras del artículo principal
representan las nubes de puntos y las componentes en ese plano. Conviene declarar de entrada:

> **La descomposición es independiente de la base.** M/S es una transformación ortogonal de L/R,
> de modo que la PCA calculada en un plano o en el otro produce exactamente los mismos
> subespacios y, al reconstruir, exactamente las mismas señales.
>
> Se usa M/S por dos razones: porque sitúa sobre un eje la magnitud de interés —cuánto contenido
> lateral tiene la grabación—, lo que hace las nubes de puntos legibles de un vistazo; y porque el
> parámetro $\beta$ y el filtro XTC son ambos **diagonales** en esa base. En general, M/S
> diagonaliza exactamente las operaciones que tratan a los dos canales de forma simétrica.

Un ejemplo de por qué esto importaba en la práctica: en el plano M/S, una grabación de alta
correlación intercanal como *So What* (Miles Davis, *Kind of Blue*) produce una nube aplastada
contra el eje M; una grabación muy lateralizada como *I Am In Love* (Shelly Manne & His Men, *At
the Black Hawk 3*) produce una nube que se abre hacia S. La diferencia se lee directamente. En el
plano L/R la misma información aparece como "cuánto se aparta la nube de la diagonal", que exige
rotar mentalmente.

Es una elección de representación, no de matemáticas.

---

## A.6 · Lo que la descomposición produce necesariamente

Aquí están las propiedades de la salida de NAE que resultan chocantes desde el marco PAE. Todas
son consecuencia de dos hechos, y de ninguna hipótesis sobre las grabaciones.

### A.6.1 Una componente correlada y otra anticorrelada, siempre

**Hecho 1 — cada componente es de rango 1.** Al reproyectar, ambos canales de una componente son
múltiplos escalares de la *misma* señal:

```math
l = a\,c, \qquad r = b\,c
\qquad \Longrightarrow \qquad
\mathrm{corr}(l, r) = \mathrm{sgn}(a\,b) = \pm 1
```

No hay valores intermedios posibles. No es improbable: es imposible.

**Hecho 2 — las componentes son ortogonales.** Con
$\mathbf{u}_1 = (\cos\theta,\ \sin\theta)$ y $\mathbf{u}_2 = (-\sin\theta,\ \cos\theta)$:

```math
\mathrm{sgn}(u_{1l}\,u_{1r}) = \mathrm{sgn}(\cos\theta\,\sin\theta), \qquad
\mathrm{sgn}(u_{2l}\,u_{2r}) = \mathrm{sgn}(-\sin\theta\,\cos\theta)
```

**Siempre opuestos.** Una componente sale con correlación $+1$ y la otra con $-1$, en cualquier
grabación y para cualquier $\theta$.

### A.6.2 El panorama de C₂ es el espejo exacto del de C₁

Con los mismos autovectores, llamando $\rho = r/l$ al balance de cada componente:

```math
\rho_1 = \cot\theta, \qquad \rho_2 = -\tan\theta
\qquad \Longrightarrow \qquad
\rho_1 \cdot \rho_2 = -1
```

En decibelios: si $C_1$ está panoramizada X dB hacia un lado, **$C_2$ está panoramizada exactamente
X dB hacia el otro**, con polaridad invertida. Y en consecuencia, cuanto más lateralizada esté la
componente principal, más lo estará —en sentido contrario— la ambiental.

Conviene subrayar que esto **no es un hallazgo sobre cómo están hechas las grabaciones**: es una
identidad algebraica que se sigue de imponer ortogonalidad. Cualquier descomposición ortogonal de
rango 1 en un plano de dos canales la produce.

### A.6.3 Esto ya está en la literatura

Ambas propiedades están derivadas y publicadas. He, Tan y Gan (2014), en su análisis de la PAE
basada en PCA, obtienen exactamente el mismo resultado:

> *Between the two channels, the primary components are amplitude panned by a factor of k, whereas
> the ambient components are negatively correlated and panned to the opposite direction of the
> primary components, as indicated by the scaling factor −1/k. Clearly, the assumption of the
> uncorrelated ambient components in the stereo signal model does not hold considering the ambient
> components extracted using PCA. This drawback is inevitable in PCA since the ambient components
> in two channels are obtained from the same basis vector.*
>
> — He, Tan y Gan (2014), §IV.A

Es decir: la estructura de salida de NAE es una propiedad conocida de la PCA aplicada a señales
estéreo. Lo que NatAmbio aporta no es la propiedad, sino **qué hacer con ella**.

### A.6.4 Una nota sobre la implementación

En la implementación real, la correlación de $C_2$ no es exactamente $-1$, sino muy próxima. El
motivo no es el redondeo numérico: cada muestra de salida recibe varias reconstrucciones
sucesivas —una por cada análisis de la ventana deslizante— y el autovector deriva ligeramente
entre ellas. La desviación respecto de $-1$ **mide la no estacionariedad de la imagen espacial**
en la ventana de análisis. La reconstrucción total sigue siendo exacta, porque
$C_1 + C_2 = (M, S)$ se cumple para cualquier base ortonormal; el solapamiento suaviza el reparto
entre componentes, no la suma.

---

## A.7 · El espacio perceptual de las componentes

![Círculo perceptual de los pares de rango 1](images/circulo_perceptual_rango1.svg)

Como $C_1$ y $C_2$ son ambas de rango 1, cada una tiene **un solo grado de libertad**: su
dirección. Descomponiendo $(1, \rho)$ en la base $\{(1,1), (1,-1)\}$, con pesos $(1+\rho)/2$ y
$(1-\rho)/2$, esa dirección se resume en un ángulo:

```math
\tan\varphi = \frac{1 - \rho}{1 + \rho}
```

De modo que el conjunto de todos los pares posibles no es un plano: es una **circunferencia**. Y
las herramientas de señales de prueba del proyecto (`tools/testing_XTC`, algoritmo 1) la recorren
de extremo a extremo partiendo de una señal mono.

| $\varphi$ | señal | percepto observado |
|---|---|---|
| **0°** | $[1,\,1]$, ILD 0 dB | **máxima localización** — centrado y compacto |
| 45° | ILD ∞, correlada | localizado, apenas abre más allá del altavoz |
| ≈63–67° | ILD 7.7–9.5 dB, anticorrelada | **máxima lateralidad** — abre más allá de los altavoces |
| **90°** | $[1,\,-1]$, ILD 0 dB | **máxima deslocalización** — centro elevado |
| ≈113–117°, 135° | espejos | |

Los valores del máximo de lateralidad están medidos con el generador de señales de prueba del
proyecto. Conviene una advertencia de unidades: ese generador produce
$`\text{out} = \text{mono}\cdot(1-g)`$ en el canal activo, de modo que el parámetro que muestra
—la sobrecancelación en dB— **no es** el desnivel entre canales. La relación es

```math
\text{ILD} = -20\log_{10}\left|1 - 10^{\,D/20}\right|
```

y en particular una sobrecancelación de +6 dB corresponde a Side puro con desnivel nulo, que es
exactamente el polo inferior del círculo.

Tres hechos estructurales:

1. El semicírculo superior es contenido correlado y el inferior anticorrelado; el diámetro
   horizontal —la panoramización extrema— es donde cambia el signo.
2. Los dos polos son ambos "centrado", de maneras opuestas: uno es el máximo del foco y el otro
   el mínimo.
3. La lateralidad **no es monótona**: es una joroba con máximos a ambos lados de $[1,-1]$. Ni el
   extremo correlado ni el anticorrelado abren la escena; abre lo que hay entre medias.

El polo inferior es, además, el único percepto del sistema que no requiere demostración: es el
del **altavoz con la polaridad invertida**, que todo el mundo ha oído. Conviene precisar la
diferencia en la misma frase, porque no son la misma situación: en el altavoz invertido está en
antifase **toda** la señal; en NatAmbio lo está sólo una componente ortogonal que convive con una
principal en fase y en general dominante.

### Resultados del barrido

Las observaciones que siguen se obtuvieron recorriendo la circunferencia con el generador de
barrido del proyecto sobre el sistema completo, con niveles igualados por sonoridad percibida y
material de referencia empleado durante meses.

**Lo que no depende del filtro.** El punto en que la imagen pasa de abrirse a cerrarse permanece
en la misma posición al variar los **dos** parámetros principales del cancelador:

| parámetro | valores probados | ¿se desplaza el punto de giro? |
|---|---|---|
| ILD del filtro | 8, 14, 20 dB | **no** |
| ITD del filtro | 120, 180, 250 µs | **no** |

**Lo que sí depende del filtro.**

- *Cuánto* abre la escena. Con ITD 250 µs abre notablemente menos, lo que resulta coherente con
  que la banda en la que el modo antisimétrico se realza sea más estrecha.
- El color tonal, coherente con el desplazamiento del peine, cuyo periodo $1/D$ pasa de unos
  8.3 kHz a unos 4.0 kHz entre los extremos probados.
- **La posición del punto de panoramización extrema**, que resulta no estar en el altavoz. Con un
  programa $(0, 1)$ el cancelador hace sonar **ambos** altavoces —uno de ellos emite la señal de
  cancelación— de modo que la imagen aparece donde la sitúan las señales que llegan a los
  tímpanos y no donde está la caja. Se desplaza de forma aproximadamente proporcional a la
  apertura total que consigue el sistema.

Esta última observación es probablemente la evidencia más directa, y la más fácil de comprobar,
de que la cancelación está operando: sin ella, panoramización extrema equivale a "en el altavoz";
con ella, la posición se desliga de la geometría física.

**Recorrido desigual.** El mayor incremento de posición percibida no se produce en el centro del
tramo sino al abandonar el extremo panoramizado, con desniveles entre canales del orden de 24 a
14 dB.

**Pistas que encajan, sin explicación completa.** Se enuncian como pistas:

1. Por encima del borde de banda del cancelador, el cociente de magnitudes entre los dos modos
   promedia 0 dB; por encima de ~1.5 kHz la lateralización la gobierna la diferencia de nivel; y
   con material anticorrelado el desfase de 180° constituye un cue temporal inutilizable en
   graves. Las tres cosas juntas situarían el juicio de posición en una banda en la que el filtro
   es neutro, lo que explicaría la invariancia observada.
2. La lateralización por diferencia de nivel satura en torno a los 15–20 dB. Ese umbral cae dentro
   del tramo en el que se observa el mayor incremento de posición.

Ninguna de las dos está comprobada. La primera admite falsación directa: limitando en banda la
señal de prueba por debajo de 1 kHz, el punto de giro debería pasar a depender del filtro.

**Resolución de la medida.** Los pasos del barrido son de 0.5 dB, equivalentes a unos 4° de
$\varphi$ en las proximidades del punto de giro. Lo establecido es por tanto que **no se desplaza
más de uno o dos pasos**, no que sea estrictamente invariante.

> **Sobre la reproducibilidad de estas observaciones.** Todas se han obtenido con herramientas
> incluidas en el repositorio público —el generador de barrido de `tools/testing_XTC`, también
> disponible como módulo LADSPA, y los generadores de filtros de `tools/xtc_filters`— sobre un
> sistema construido con hardware corriente. No se ha empleado instrumental externo ni ningún
> montaje que no pueda replicarse.
>
> No se dispone todavía de una explicación completa de por qué los puntos de giro caen donde caen.
> Se publican los resultados en este estado precisamente para que puedan contrastarse: quien monte
> un sistema equivalente puede repetir las medidas, poner a prueba las hipótesis enunciadas,
> proponer otras distintas, u obtener resultados que no coincidan. Cualquiera de esas tres cosas
> sería igual de informativa que una confirmación.

### La segunda dimensión

Todo lo anterior ocurre **sobre** la circunferencia. Reproducir $\alpha\,C_1 + \beta\,C_2$ saca el
punto fuera: la elipse de covarianza deja de ser una recta y se abre. Ésa es la segunda dimensión
—la **excentricidad**— y es lo que mueve el balance entre componentes.

El espacio completo es por tanto un disco: el **ángulo** dice qué relación interaural se presenta
y lo fija la grabación; el **radio** dice cuán determinada es esa relación y lo fija el ajuste.

### Dos rutas distintas hacia lo no localizable

De ahí se sigue la distinción que estructura toda la comparación de A.9:

| ruta | posición | correlación | carácter |
|---|---|---|---|
| **Antifase** | sobre la circunferencia, $\varphi \to 90°$ | $-1$ | relación determinada pero no mapeable a una dirección |
| **Decorrelación** | hacia el centro del disco | $0$ | señales genuinamente distintas en cada oído |

NatAmbio emplea la primera. La literatura PAE persigue la segunda.

### Comprobación experimental: reproducir la definición PAE de ambiente

Se implementó una variante de NatAmbio en la que las ganancias de $C_1$ y $C_2$, en lugar de
fijarse a mano, **se autoajustan trama a trama para que la correlación L/R de salida sea 0**. La
condición es sencilla:

```math
\alpha^{2}\lambda_1 = \beta^{2}\lambda_2
```

es decir, potencia reproducida igual en ambas componentes, lo que convierte la elipse de
covarianza en un círculo. El resultado es, por construcción, la definición de ambiente que emplea
la literatura PAE, realizada sobre el propio sistema.

Escuchada a través del XTC, con *I Am In Love* como material y niveles igualados por sonoridad
percibida:

- La **trompeta** se percibe claramente localizada en el altavoz izquierdo y la **batería** en el
  derecho, ninguna de las dos abriendo más allá del recinto.
- El resto del material se sitúa de forma difusa en el centro.
- La escena no se despliega: donde el ajuste habitual lleva la imagen de unos 40° a más de 120°,
  aquí no ocurre.

Las tres observaciones corresponden con precisión a tres regiones del círculo. El blanqueo es una
operación de trama y de banda ancha: elimina el **sesgo agregado** de la imagen, no la
panoramización de cada fuente. Trompeta y batería siguen en $\varphi = 45°$ y $135°$, la esquina
que el modelo etiqueta como *localizado pero apenas abre*, y el residuo se va al centro del disco.

**Y el motivo tiene mecanismo.** El XTC modifica la razón entre los dos modos propios. Cuando
ambos modos transportan la **misma forma de onda** —que es lo que significa $\mathrm{corr} =
\pm 1$— cambiar esa razón cambia la posición de la imagen. Cuando transportan formas de onda
**independientes** —$\mathrm{corr} = 0$— cambiar la razón sólo reequilibra un campo difuso:
no hay imagen que mover.

Conviene el matiz: el cancelador sí actúa sobre una señal de correlación nula, puesto que escala
ambos modos. Lo que no hace es **producir imagen**, porque no se le ha entregado ninguna relación
interaural que reproducir.

> De modo que la conclusión no es que la definición PAE de ambiente sea errónea. Es que **en esta
> arquitectura resulta inerte**: no se proyecta a ningún punto virtual de la escena porque no
> contiene la magnitud sobre la que el sistema de reproducción opera.

Esta comprobación acota además, de forma cualitativa, uno de los dos puntos abiertos del círculo:
con panoramización extrema y contenido correlado, la fuente **no abre** — se queda en el altavoz.

### Definición de ambiente, y su dominio de validez

Con esto se puede enunciar con precisión la definición que NatAmbio utiliza:

> **Ambiente** es aquí la componente ortogonal cuya reproducción resulta predominantemente **no
> localizable**.

Se dice *no localizable* y no *difuso* de forma deliberada. "Difuso" es una propiedad de un campo
sonoro —energía llegando por igual de todas partes, baja coherencia entre puntos— y $C_2$ no es
eso: es máximamente coherente, sólo que con el signo cambiado. "No localizable" es una propiedad
de un percepto, y $C_2$ sí lo es.

La definición tiene además un dominio de validez que la propia geometría delimita, porque $C_2$ se
sitúa en $\varphi_2 = 45° + \theta$:

- **Imagen principal centrada** ($\theta = 45°$) → $\varphi_2 = 90°$, Side puro. La definición es
  **exacta**.
- **Imagen lateralizada** ($\theta$ pequeño) → $\varphi_2$ desciende hacia 45° y **$C_2$ se vuelve
  localizable**. La definición se degrada de forma monótona.

Ése es precisamente el modo de fallo del caso *I Am In Love*, y la razón de existir del modo beta.

Hay también un límite estructural que conviene dejar escrito: si la imagen principal está
centrada, $C_2$ es Side puro y no tiene componente Mid que escalar, de modo que ninguna operación
de anchura puede desplazarlo del polo. Llevarlo al máximo de lateralidad exigiría **añadirle**
contenido Mid, lo cual sería sintetizar y la premisa de A.4 lo prohíbe.

---

## A.8 · El modo beta

### A.8.1 Qué problema resuelve

La PCA no sabe qué es un instrumento. En una grabación con panoramización extrema —*I Am In Love*
es el caso de estudio del artículo principal— parte del contenido musical de primer plano puede
acabar en la componente secundaria. En el dipolo frontal esto no es grave, porque $C_1$ y $C_2$ se
reproducen juntos y coexisten perceptualmente. En el **dipolo trasero, donde $C_1$ no está**, sí
lo es: un instrumento identificable detrás del oyente rompe la ilusión.

El modo beta existe para eso, y **sólo tiene sentido en el dipolo trasero o muy lateral de un
PanAmbio.** Fuera de esa arquitectura no significa nada.

### A.8.2 Qué hace

```math
\beta = 0.55 + 0.45\,\bigl|\mathrm{corr}(L, R)\bigr|
\qquad\text{medida sobre una ventana larga}
```

```math
S = \beta\,(L - R) \qquad\text{aplicado \textbf{antes} de calcular la covarianza}
```

Cuanto más lateralizada es la grabación (correlación baja), más se reduce el peso de la componente
Side. Y la reducción **no se deshace** al reconstruir en L/R.

De ahí salen dos efectos distintos que conviene no confundir:

1. **Rota el eje principal hacia M.** Al reescalar S en la matriz de covarianza, se sesga la
   estimación para que más contenido lateral se clasifique como principal y no llegue a $C_2$.
2. **Atenúa S en la salida.** Lo que sí llega a $C_2$ sale escalado por $\beta$.

El primero cambia *qué* se separa; el segundo, *cuánto* se emite. No son redundantes.

### A.8.3 Visto desde fuera

Escalar S por $\beta$ equivale, en coordenadas L/R, a aplicar a la entrada la matriz

```math
\mathbf{W}_{\beta} = \frac{1}{2}\begin{bmatrix} 1+\beta & 1-\beta \\ 1-\beta & 1+\beta \end{bmatrix}
```

que es el **control de anchura estéreo** de toda la vida: $\beta = 1$ es la identidad, $\beta = 0$
es mono. Puede comprobarse que $`\mathbf{T}\,\mathbf{W}_{\beta} = \mathrm{diag}(1,\beta)\,\mathbf{T}`$,
siendo $\mathbf{T}$ la transformada M/S.

Así que beta, descrito con precisión, es un **control de anchura estéreo adaptativo, gobernado por
la correlación intercanal medida, aplicado antes del análisis y no revertido en la
reconstrucción**. La operación es estándar; lo inusual es *dónde* se aplica: antes de la
descomposición, no después. Ahí está la idea.

### A.8.4 Lo que no está explicado

Beta es **empirismo puro**. Surgió de escuchar y ajustar, no de un criterio de optimización. A
día de hoy siguen sin justificación:

- Por qué los coeficientes son 0.55 y 0.45, es decir, por qué el rango útil de $\beta$ resultó ser
  $[0.55,\ 1]$.
- Por qué la ventana de medida de la correlación es de 20 bloques.
- Por qué se usa el **valor absoluto** de la correlación. Con $|\mathrm{corr}|$, una
  grabación fuertemente *anti*correlada recibe poca reducción de S, igual que una fuertemente
  correlada. Si el criterio pretendido era "cuanto más lateral, menos S", el valor absoluto lo hace
  ciego al signo. Puede ser correcto o puede ser un residuo del proceso de desarrollo; no está
  decidido.

Estos huecos se dejan escritos como huecos.

Hay sin embargo un resultado empírico que sí merece constar, porque es de naturaleza distinta a
una preferencia. **Antes de beta**, el mando de nivel del dipolo trasero había que reajustarlo
disco a disco; **después**, un solo ajuste aguanta en toda la colección. Un mando que exige
corrección continua está compensando a mano una dependencia que el algoritmo no conoce; un mando
que se queda quieto indica que esa dependencia ya está dentro. Es un dato **conductual**, no
perceptual —se mide en si se toca el mando o no—, longitudinal y falsable: bastaría un disco que
obligara a reajustar para romperlo.

---

## A.9 · Dónde encaja esto respecto a la literatura PAE

### A.9.1 El modelo establecido

La literatura de *primary-ambient extraction* modela la señal estéreo como

```math
x_0 = p_0 + a_0, \qquad x_1 = p_1 + a_1
```

con cuatro hipótesis [Goodwin y Jot 2007; Faller 2006; He, Tan y Gan 2014]:

| | hipótesis |
|---|---|
| 1 | La componente principal está **correlada** entre canales: $p_1 = k\,p_0$ |
| 2 | Las componentes ambientales están **decorreladas** entre sí: $a_0 \perp a_1$ |
| 3 | Principal y ambiente son **ortogonales**: $p \perp a$ |
| 4 | Las energías ambientales están **equilibradas**: $\|a_0\| \approx \|a_1\|$ |

La hipótesis 2 es la definición operativa de "ambiente" en ese marco: ambiente es lo que **no**
está correlado, porque lo que no está correlado no se puede localizar.

### A.9.2 Por qué esa definición es la correcta *para ellos*

El destino de esa literatura es el *upmix*: repartir una señal estéreo entre cinco o más altavoces.
Y ahí la decorrelación no es una preferencia estética, es la única herramienta que funciona:

> **La decorrelación es robusta frente a caminos de transmisión desconocidos.** Fuentes
> incorreladas se suman en energía, de modo que la correlación resultante en los tímpanos queda
> determinada por los niveles y no por las fases. Funciona con cualquier sala, cualquier posición
> de escucha y cualquier diafonía.
>
> **La anticorrelación es un efecto de interferencia.** Depende de que las fases lleguen como uno
> espera, y por tanto exige conocer y controlar los caminos de transmisión.

Un sistema multialtavoz no conoce sus caminos de transmisión ni pretende controlarlos. Por eso
$\mathrm{corr} = 0$ es, para esa arquitectura, la respuesta correcta.

Hay además una segunda razón, independiente: **el upmix expande el número de canales**. Pasar de
2 a 5 obliga a producir material que no está en la grabación, y la decorrelación es exactamente
el mecanismo para hacerlo. Es creación de información, y en ese contexto es legítima.

### A.9.3 Por qué NatAmbio termina en −1

Por dos razones de peso muy distinto.

**Primera, y suficiente: la restricción de diseño.** NatAmbio no puede decorrelar, porque
decorrelar es sintetizar información que no está en la grabación (A.4). Esto no depende de ningún
argumento sobre el sistema de reproducción: se sigue de la premisa. Y NatAmbio no expande canales:
descompone dos en dos componentes ortogonales y las redistribuye.

Eliminada la decorrelación artificial, y siendo la descomposición ortogonal de rango 1, la
correlación $-1$ no es una elección. Es la única salida posible (A.6.1).

**Segunda, y complementaria: la arquitectura.** Un cancelador de diafonía simétrico y la planta
acústica que invierte comparten los autovectores $(1,1)$ y $(1,-1)$, con autovalores recíprocos
$1 \pm G$. Cualquier componente atraviesa por tanto el sistema como una parte simétrica y otra
antisimétrica escaladas de forma independiente, y **el signo de la correlación intercanal
determina cuál de los dos autovalores gobierna su paso.**

> El desarrollo de este punto —la estructura *shuffler*, por qué el diseño del cancelador es un
> problema escalar y por qué toda la regularización vive en un solo autovalor— está en la sección
> A.9 del [anexo de XTC](../xtc/xtc_filters_anexo_es.md), y no se repite aquí.

### A.9.4 La inversión defecto ↔ especificación

Es instructivo leer la evaluación que la propia literatura hace de la PAE basada en PCA. He, Tan
y Gan (2014) resumen sus fortalezas y debilidades así:

**Fortalezas de PCA**
- No introduce distorsión en la componente principal extraída.
- **No hay fuga de la componente principal en la componente ambiental extraída** (LSR = 0).
- Principal y ambiente resultan mutuamente incorreladas.

**Debilidad de PCA**
- La componente ambiental queda fuertemente panoramizada y anticorrelada.

Leídas contra los requisitos de NatAmbio, **las tres fortalezas son la especificación y la única
debilidad es la característica de diseño**.

La segunda fortaleza merece atención aparte, porque responde a la objeción más frecuente que se
le hace a NAE —que instrumentos de primer plano se cuelen en el canal ambiental—: entre los
estimadores lineales estudiados, **PCA es el que tiene fuga primaria nula** en la componente
ambiental. No pequeña: nula, y demostrado en el mismo trabajo que la critica por lo otro.

Y su propia recomendación para el estimador de mínima fuga —que para la componente ambiental es
idéntico a PCA— nombra el caso de uso: aplicaciones en las que se emplean técnicas de renderizado
o reproducción **distintas** para la componente principal y la ambiental. Un dipolo frontal y un
dipolo trasero con procesados distintos son exactamente eso.

### A.9.5 Comparación formal

Bajo el modelo PAE con $k > 0$ y energías ambientales iguales, la correlación intercanal
reconstruible es

```math
\mathrm{corr}(x_0, x_1) = \frac{k\,P_p}{\sqrt{(P_p + P_a)\,(k^{2}P_p + P_a)}}
```

Bajo la descomposición ortogonal de NAE, con potencias $P_1 \ge P_2$ y ángulo principal $\theta$,

```math
\mathrm{corr}(x_0, x_1) =
\frac{(P_1 - P_2)\,\sin\theta\,\cos\theta}
{\sqrt{\bigl(P_1\cos^{2}\theta + P_2\sin^{2}\theta\bigr)\bigl(P_1\sin^{2}\theta + P_2\cos^{2}\theta\bigr)}}
```

Y el cumplimiento de las cuatro hipótesis:

| hipótesis | NAE |
|---|---|
| 1. Principal correlada | **Cumple exactamente** (rango 1 por construcción) |
| 2. Ambiente decorrelado | **Incumple al máximo, deliberadamente** ($\pm 1$) |
| 3. Principal ⊥ ambiente | **Cumple exactamente** ($c_1 \perp c_2$ por PCA) |
| 4. Energías equilibradas | **Incumple** — y ese incumplimiento *es* el panorama espejo (A.6.2) |

NAE cumple exactamente las dos hipótesis que definen la calidad de la separación, e incumple las
dos que definen "ambiente" según un criterio pensado para otra arquitectura de reproducción.

### A.9.6 Lo que la literatura hace y NatAmbio no puede hacer

Ninguno de los estimadores estudiados alcanza $\mathrm{corr} = 0$ en el ambiente extraído.
La conclusión de la propia literatura es que hacen falta técnicas de posprocesado —decorrelación y
reescalado— para conseguirlo.

Ese posprocesado es precisamente lo que la restricción de diseño de NatAmbio prohíbe. La
divergencia, por tanto, no es un descuido por ninguna de las dos partes: es una consecuencia de
premisas distintas, y ambas son coherentes con su propio destino.

### A.9.7 El mismo eje, dos veces

Conviene señalar que ésta no es la única tradición de la que NatAmbio se separa por el mismo
motivo. Ambiophonics —de la que procede toda la arquitectura— genera el camino ambiental
convolucionando el sonido directo con la respuesta impulsional de una sala real, tomada de una
biblioteca de auditorios. Es síntesis explícita, igual que la decorrelación, sólo que con una
fuente distinta.

| enfoque | origen del ambiente |
|---|---|
| Ambiophonics | lo **crea** por convolución con la RI de una sala real |
| Literatura PAE | lo **crea** por decorrelación artificial |
| **NatAmbio** | **no lo crea**: extrae el que ya está y lo proyecta |

La restricción de diseño es, por tanto, el **único eje distintivo del proyecto**, y opera en las
dos direcciones. El contraste con Ambiophonics se desarrolla en la sección A.10 del
[anexo de XTC](../xtc/xtc_filters_anexo_es.md).

### A.9.8 La conclusión

Sometida a comprobación directa (A.7), la definición PAE de ambiente reproducida sobre este
sistema no despliega escena: las fuentes panoramizadas se quedan en sus altavoces y el resto se
difumina en el centro. No porque la definición esté mal planteada —para un array multialtavoz es
la correcta— sino porque describe una señal que carece de la magnitud sobre la que actúa una
cadena con cancelación de diafonía.

> **La definición de ambiente como contenido decorrelado no es errónea. Es inerte en esta
> arquitectura.**

Y su recíproca es la razón de ser de NAE: en una cadena que entrega relaciones interaurales, la
componente que no se puede localizar no es la que carece de relación, sino la que tiene una que
el oído no sabe mapear a una dirección.

---

## A.10 · Lo que no sabemos

Se listan aquí, deliberadamente, los puntos abiertos.

**Pendiente de medir**

- NAE no ha pasado por el banco de medidas sintético de He, Tan y Gan (voz más ruido incorrelado,
  factor de panorama fijo, relación de potencias barrida). Hacerlo permitiría situar a NAE en su
  Tabla I con sus propias métricas —ESR, DSR, ISR, LSR, ICC, ICLD— y hacer la comparación
  cuantitativa en lugar de estructural. Es la carencia principal de este documento.
- No se ha comparado el análisis en banda completa con un análisis por bandas sobre el mismo
  material. La predicción es que el análisis por bandas produzca una correlación ambiental
  claramente por debajo de 1 en valor absoluto, por promediado entre bandas con orientaciones de
  autovector distintas; no está comprobado.
- No se ha cuantificado qué compra y qué cuesta el modo beta en términos de esas métricas.

**Pendiente de acotar en el círculo perceptual (A.7)**

- **Inherentes al modelo:** los polos. $[1,1]$ a 0 dB es máxima localización y $[1,-1]$ a 0 dB es
  máxima deslocalización. Son los extremos del eje de foco.
- **Medido:** el máximo de lateralidad se da con un desnivel entre canales de **7.7 a 9.5 dB**, y
  **no se desplaza** al variar ni el ILD (8–20 dB) ni el ITD (120–250 µs) del filtro. Lo que cambia
  con esos parámetros es cuánto abre y el color tonal, no dónde está el máximo.

  La invariancia admite una lectura favorable al sistema: bajo cancelación aproximadamente ideal
  las señales en los oídos son el programa, de modo que el percepto queda determinado por el
  desnivel y la fase del programa y no por los parámetros internos del filtro. Que el punto de
  giro no siga al filtro es indicio de que el filtro **entrega** el programa en lugar de imponerle
  su firma.

- **Por acotar:**
  1. Cuánto abre, en grados, el punto de panoramización extrema. Se sabe que **su posición no es
     la del altavoz y depende del ajuste del cancelador**, pero no está cuantificado. Existe además
     una observación aparentemente discordante: con la señal de prueba aislada el punto se desplaza
     claramente, mientras que en el experimento de blanqueo las fuentes muy panoramizadas se
     percibían pegadas a sus altavoces. La reconciliación plausible es que en el segundo caso el
     cue quedaba diluido entre material decorrelado que no se sitúa, pero ambas observaciones se
     dejan escritas por separado en lugar de forzarlas a coincidir.
  2. Por qué el recorrido es desigual, con el mayor incremento de posición al abandonar el extremo
     panoramizado. La saturación de la lateralización por nivel encaja numéricamente, pero no está
     comprobada.
  3. Si el máximo observado es un extremo real de extensión lateral o, en parte, el punto donde la
     imagen es **simultáneamente lateral y lo bastante definida como para situarla**: cerca del
     polo la deslocalización impide juzgar la lateralidad.

Conviene señalar que la rama anticorrelada tiene una particularidad experimental útil: la
correlación se mantiene constante en $-1$ a lo largo de todo el tramo, de modo que lo que varía es
**únicamente el ILD a coherencia interaural fija**. Es una condición de aislamiento poco habitual.

**Pendiente de explicación**

- Los coeficientes de beta y su ventana temporal (A.8.4).
- El uso del valor absoluto de la correlación en beta (A.8.4).
- Dónde surgen los límites perceptuales del círculo. El modelo permite **describir** los perceptos
  como una construcción sobre la combinación lineal de $[1,1]$ y $[1,-1]$, y sitúa cada uno en una
  coordenada; no explica por qué los máximos caen donde caen. Se mantiene aquí la regla de trabajo
  del proyecto: la percepción gobierna el resultado, y el andamiaje matemático llega después o no
  llega.

**Fuera del alcance de este documento**

- La evaluación perceptual es de sujeto único y no ciega. No se presenta como otra cosa.
- No se ha implementado ni evaluado ningún método PAE alternativo, de modo que este documento no
  contiene ninguna afirmación comparativa sobre su rendimiento práctico.
- El detalle del comportamiento del cancelador de diafonía frente a contenido correlado y
  anticorrelado pertenece a la documentación de XTC.

---

## A.11 · La respuesta a las preguntas grandes

Buena parte de lo que hace NatAmbio se puede razonar. Otra parte, no: se ajustó escuchando, y su
justificación última es que funciona en el sistema donde se desarrolló.

Por eso el proyecto no publica sólo un modelo. Publica la implementación en tiempo real, el
módulo LADSPA para procesado offline, los generadores de filtros XTC, las herramientas de señales
de prueba y los guiones de medida. Y por eso el hardware propuesto es deliberadamente barato y
sencillo: unos altavoces corrientes, una interfaz multicanal y un ordenador modesto.

> La respuesta final a las preguntas de fondo —si esto suena mejor, si la componente
> anticorrelada se percibe como ambiente, si el modo beta hace lo que pretende— no está en este
> documento. **Está en escucharlo.** El objetivo de publicarlo es que otras personas puedan
> comprobar en sus salas y con sus oídos lo que hasta ahora sólo se ha comprobado en uno.

---

## A.12 · Referencias

> La bibliografía de la cancelación de diafonía —origen, formulación, regularización y validación
> experimental— está en la sección A.13 del [anexo de XTC](../xtc/xtc_filters_anexo_es.md). Aquí se
> recogen la línea de la que procede el proyecto, la literatura PAE y la percepción de la
> coherencia interaural.

### La línea de la que viene este trabajo

1. **Miller, R. III** (2002). "Compatible PanAmbiophonic 4.1 and PerAmbiophonic 6.1 Surround
   Sound for Advanced Television — Beyond ITU 5.1". *SMPTE 144th Technical Conference*, Pasadena.
2. **Miller, R. III** (2002). "Contrasting ITU 5.1 and PanAmbiophonic 4.1 Surround Sound Recording
   Using OCT and Sphere Microphones". *AES 112th Convention*, Múnich, preprint 5577.
3. **Glasgal, R. y Miller, R.** "Surround Ambiophonic Recording and Reproduction". *AES 24th
   International Conference on Multichannel Audio*.
4. **Glasgal, R.** (2001). "The Ambiophone — Derivation of a Recording Methodology Optimized for
   Ambiophonic Reproduction". *AES 19th International Conference*, Schloss Elmau.
5. **Glasgal, R.** (2007). "360° Localization via 4.x RACE Processing". *AES 123rd Convention*.
6. **Glasgal, R.** "Recursive Ambiophonic Crosstalk Elimination (RACE)". Ambiophonics Institute.

### Contexto: *primary-ambient extraction*

7. **Avendaño, C. y Jot, J.-M.** (2004). "A Frequency-Domain Approach to Multichannel Upmix".
   *JAES* 52(7/8), pp. 740–749.
8. **Goodwin, M. M. y Jot, J.-M.** (2007). "Primary-Ambient Signal Decomposition and Vector-Based
   Localization for Spatial Audio Coding and Enhancement". *ICASSP 2007*, pp. 9–12.
9. **Merimaa, J., Goodwin, M. M. y Jot, J.-M.** (2007). "Correlation-Based Ambience Extraction
   from Stereo Recordings". *AES 123rd Convention*, paper 7282.
10. **Faller, C.** (2006). "Multiple-Loudspeaker Playback of Stereo Signals". *JAES* 54(11),
    pp. 1051–1064.
11. **He, J., Tan, E.-L. y Gan, W.-S.** (2014). "Linear Estimation Based Primary-Ambient Extraction
    for Stereo Audio Signals". *IEEE/ACM TASLP* 22(2), pp. 505–517.
12. **He, J., Tan, E.-L. y Gan, W.-S.** (2015). "Time-Shifting Based Primary-Ambient Extraction for
    Spatial Audio Reproduction". *IEEE/ACM TASLP* 23(10), pp. 1576–1588.
13. **He, J.** (2017). *Spatial Audio Reproduction with Primary Ambient Extraction*. SpringerBriefs,
    Springer.
14. **Ibrahim, K. M. y Allam, M.** (2016). "Primary-Ambient Extraction in Audio Signals Using
    Adaptive Weighting and Principal Component Analysis". *SMC*.
15. **Briand, M., Virette, D. y Martin, N.** (2006). "Parametric Coding of Stereo Audio Based on
    Principal Component Analysis". *DAFx-06*, Montreal.

### Percepción de la coherencia interaural

16. **Blauert, J. y Lindemann, W.** (1986). "Spatial Mapping of Intracranial Auditory Events for
    Various Degrees of Interaural Coherence". *JASA* 79(3), pp. 806–813.
17. **Blauert, J.** (1997). *Spatial Hearing: The Psychophysics of Human Sound Localization*.
    MIT Press.
18. **Whitmer, W. M., Seeber, B. U. y Akeroyd, M. A.** "Measuring the Apparent Width of Auditory
    Sources in Normal and Impaired Hearing".
19. **Barron, M. y Marshall, A. H.** (1981). "Spatial Impression Due to Early Lateral Reflections
    in Concert Halls". *J. Sound and Vibration* 77(2).

> **Nota sobre las referencias.** Los datos bibliográficos de este listado proceden en parte de
> fuentes secundarias y conviene verificarlos contra la AES E-Library y IEEE Xplore antes de
> considerar definitivo este documento.
