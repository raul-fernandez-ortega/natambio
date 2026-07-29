# Aplicación de NatAmbio XTC en entornos no simétricos

**Autor:** Raúl Fernández Ortega  
**Fecha:** julio de 2026

En la presente nota técnica se amplía el diseño de filtros XTC para entornos estéreo convencionales al caso de disposiciones en las que, por el motivo que sea, no se cumple la deseada simetría en la ubicación de los altavoces.

## Notación

| Símbolo | Significado |
|---|---|
| $H_{ll},\ H_{rr}$ | Caminos acústicos directos (altavoz → oído del mismo lado) |
| $H_{lr},\ H_{rl}$ | Caminos acústicos cruzados (altavoz → oído contrario) |
| $G_l = H_{lr}/H_{ll}$ | Función de transferencia cruzada normalizada del altavoz izquierdo |
| $G_r = H_{rl}/H_{rr}$ | Función de transferencia cruzada normalizada del altavoz derecho |
| $b = H_{rr}/H_{ll}$ | Balance del sistema estéreo asimétrico |
| $g_x,\ S_x$ | Parte de banda ancha y forma espectral de $G_x = g_x \ast S_x$ |
| $\bar{S}$ | Forma espectral de la ida y vuelta (media de las log-magnitudes de $S_l$ y $S_r$) |
| $P = G_l \ast G_r$ | Operador de ida y vuelta: un escalón completo de la escalera (al implementarlo, $g_l \ast g_r \ast \bar{S}$) |
| $\mathbf{H}$ | Matriz de transferencia acústica del sistema asimétrico |
| $\mathbf{M}$ | Matriz de acoplamiento relativo normalizado (equivalente asimétrico de $\mathbf{C}_G$) |
| $\mathbf{D}$ | Matriz diagonal de balance, $\mathbf{D} = \operatorname{diag}(1,\ b)$ |
| $\mathbf{F}_{XTC}$ | Matriz de filtrado XTC (filtros directos y cruzados) |
| $\Theta_l,\ \Theta_r$ | Azimut de incidencia de cada altavoz |
| $\delta$ | Impulso unitario (elemento neutro de la convolución) |
| $\ast$ | Operador convolución |
| $N$ | Número de términos (iteraciones) del sumatorio |

## Planteamiento del modelo y desarrollo matemático

Volvemos a comenzar por el esquema sonoro de un sistema estéreo básico donde se visualizan los caminos acústicos de la diafonía:

![Escena sonora estéreo](images/esquema_XTC_01.svg)


Como ya se desarrolló en [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md), se puede definir la matriz general de los caminos acústicos como:

```math
\mathbf{H} = \begin{bmatrix} H_{ll} & H_{rl} \\ H_{lr} & H_{rr} \end{bmatrix}
```

Si extraemos del problema los caminos acústicos directos, que serán [tratados por ecualización mediante DRC y no por XTC](xtc_filters_es.md#qu%C3%A9-se-invierte-y-qu%C3%A9-no-separaci%C3%B3n-entre-xtc-y-drc), podemos hacer una primera descomposición de la matriz $\mathbf{H}$:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & {H_{rl}}/{H_{ll}} \\ {H_{lr}}/{H_{ll}} & {H_{rr}}/{H_{ll}} \end{bmatrix}
```

Asumiendo que ambos caminos directos van a ser posteriormente ecualizados mediante DRC para obtener una respuesta semejante en el punto de escucha, se puede aproximar:

```math
{H_{rr}} = b \cdot H_{ll}
```

Siendo $b = H_{rr}/H_{ll}$ la relación lineal entre los niveles de ambos caminos directos del sistema asimétrico, lo que usualmente se denomina el "balance" del sistema estéreo.

Conviene detenerse en el alcance de esta aproximación. El cociente $H_{rr}/H_{ll}$ es, en rigor, una función de transferencia completa: recoge tanto la diferencia de respuesta en frecuencia entre ambos caminos directos como la diferencia de tiempo de vuelo asociada a las distintas distancias altavoz–oyente propias de una disposición asimétrica. Ahora bien, esa función de transferencia forma parte de la inversión de $H_{ll}$ y $H_{rr}$, es decir, del tratamiento de los caminos directos, que —según la separación entre XTC y DRC establecida en la nota principal— corresponde íntegramente a la etapa DRC y no al filtrado XTC. Una vez que el DRC ha igualado la forma espectral y el retardo de ambos caminos directos, lo único que resta de ese cociente es un escalar real: la ganancia relativa entre canales. Por ello el modelo XTC modeliza finalmente $b$ como un escalar y asume el desajuste acústico residual que pueda subsistir, tanto porque no está provocado por el filtrado XTC como porque el DRC, por su propia naturaleza, no trabaja con el balance entre canales.

Además, como ya se definió en [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md#an%C3%A1lisis-del-problema-y-resoluci%C3%B3n), se puede utilizar el término $G$:

```math
G = \frac{H_{cross}}{H_{direct}}
```

```math
G_{l} = \frac{H_{lr}}{H_{ll}}
```

```math
G_{r} = \frac{H_{rl}}{H_{rr}}
```

La matriz $\mathbf{H}$ se descompone como:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & b \cdot G_{r} \\ G_{l} & b \end{bmatrix}
```

La matriz a invertir mediante el desarrollo recursivo presenta dos diferencias respecto al caso simétrico debidas a la propia asimetría del sistema: la primera es que $G_{l} \neq G_{r}$ y la segunda es que hay que incluir un balance $b$ en uno de los dos canales para igualar la presión sonora en el punto de escucha.

Obsérvese que el modelo no cambia. La única diferencia respecto al caso simétrico consiste en que el sistema pasa de utilizar una única función $G$ a utilizar dos funciones distintas, $G_{l}$ y $G_{r}$, una para cada altavoz.

Continuando con el planteamiento de [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md#desarrollo-del-dise%C3%B1o-final), tanto $G_{l}$ como $G_{r}$ se pueden expresar como dependientes de:

```math
G_{l} = \delta \left ( \text{ITD}_{l}, \text{ILD}_{l} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{l} \right), \text{ILD}_{avg} \left ( \Theta_{l} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{l}, f \right )
```

```math
G_{r} = \delta \left ( \text{ITD}_{r}, \text{ILD}_{r} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{r} \right), \text{ILD}_{avg} \left ( \Theta_{r} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{r}, f \right )
```

La parametrización de cada una de estas funciones puede seguir el mismo modelo desarrollado en la nota principal XTC para NatAmbio.

En cuanto a la matriz $\mathbf{H}$, podemos seguir descomponiéndola en:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & G_{r} \\ G_{l} & 1 \end{bmatrix} \cdot \begin{bmatrix} 1 & 0 \\ 0 & b \end{bmatrix} = H_{ll} \cdot \mathbf{M} \cdot \mathbf{D}
```

Con lo que la matriz a invertir para obtener los filtros XTC es:

```math
\mathbf{M} = \begin{bmatrix} 1 & G_{r} \\ G_{l} & 1 \end{bmatrix}
```

La inversión de todo el camino acústico $\mathbf{H}$ es:

```math
\mathbf{H}^{-1} = \left( H_{ll} \cdot \mathbf{M} \cdot \mathbf{D} \right)^{-1} = H_{ll}^{-1} \cdot \begin{bmatrix} 1 & 0 \\ 0 & 1/b \end{bmatrix} \cdot \mathbf{M}^{-1}
```

Nótese el orden de los factores: $H_{ll}$ es un escalar y conmuta, pero $\mathbf{D}$ y $\mathbf{M}$ no, de modo que $\mathbf{D}^{-1}$ debe premultiplicar a $\mathbf{M}^{-1}$. Como se verá más adelante, esta posición es precisamente la que hace que la ganancia $1/b$ actúe sobre la señal entregada al altavoz derecho.

Coherentemente con la separación entre XTC y DRC de la nota principal, NatAmbio no implementa la inversa acústica completa: el factor $H_{ll}^{-1}$ se delega al DRC y la matriz de filtrado XTC es

```math
\mathbf{F}_{XTC} = \mathbf{D}^{-1} \cdot \mathbf{M}^{-1} = \frac{1}{1 - G_{l} G_{r}} \begin{bmatrix} 1 & -G_{r} \\ -G_{l}/b & 1/b \end{bmatrix}
```

de modo que $\mathbf{H} \cdot \mathbf{F}_{XTC} = H_{ll} \cdot \mathbf{I}$: cada oído recibe únicamente la señal que le corresponde, con el mismo nivel en ambos, y a través de los caminos directos naturales, que permanecen intactos.

## Aplicación en NatAmbio

Como ya hemos demostrado en la nota principal sobre XTC para NatAmbio, la inversión de la matriz $\mathbf{M}$ es equivalente a su solución recursiva. Aplicando el mismo desarrollo recursivo descrito en la nota principal al caso general $G_l \neq G_r$, se obtiene:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^i
```

```math
F^{cross}_l = - {G_{r}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{r}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

```math
F^{cross}_r = - {G_{l}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{l}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

Siendo $P$ el operador de ida y vuelta, es decir, el paso completo de la escalera recursiva. Por definición es el producto de las dos funciones cruzadas normalizadas:

```math
P = G_{l} \ast G_{r}
```

que es exactamente el trayecto que describe un escalón de la recursión: la señal sale del altavoz izquierdo hacia el oído derecho —factor $G_{l}$— y vuelve desde el altavoz derecho hacia el oído izquierdo —factor $G_{r}$—. Es también el producto sobre el que se establece la condición de convergencia, y el objeto simétrico bajo intercambio de canales del que dependen los dos filtros directos.

### Descomposición del operador de ida y vuelta

La expresión $P = G_{l} \ast G_{r}$ es la que da sentido al desarrollo, pero no es la que se lleva directamente a código. En el paso a implementación cada función $G$ se separa en su parte de banda ancha y su forma espectral, $G_x = g_x \ast S_x$, y entra en juego el convenio establecido en la nota principal —[aplicación del espectro ILD en la serie](xtc_filters_es.md#aplicaci%C3%B3n-del-espectro-ild-en-la-serie)—: el espectro se aplica una sola vez por escalón, mientras que el retardo y la atenuación de banda ancha sí siguen la ley completa de potencias. El operador de ida y vuelta se genera entonces como

```math
P = g_l \ast g_r \ast \bar{S}
```

esto es: los retardos de ambos lados se suman, las atenuaciones se multiplican, y allí donde el producto $G_{l} \ast G_{r}$ acumularía $S_l \ast S_r$ el espectro interviene una única vez, a través de una forma promedio $\bar{S}$. Con ello el término $i$ del filtro directo vale $(g_l g_r)^i \ast \bar{S}^i$ y el término $i$ de cada cruzado $g_r (g_l g_r)^{i-1} \ast S_r \ast \bar{S}^{i-1}$, de modo que el número de aplicaciones del espectro es $i$ en todos los casos, con el mismo índice $i$ numerando el mismo escalón en el filtro directo y en los cruzados.

Queda por determinar qué forma espectral corresponde a $\bar{S}$. Como la ida y vuelta atraviesa una sombra acústica de cabeza por cada lado, y solo hay sitio para una aplicación, la elección natural es la media de ambas log-magnitudes. Dado que el modelo empírico de $ILD_{spectrum}$ es lineal en el producto $\alpha \sin\Theta$, esa media se obtiene sin promediar respuestas: basta evaluar el mismo modelo con

```math
\bar{\kappa} = \tfrac{1}{2} \left( \alpha_l \sin\Theta_l + \alpha_r \sin\Theta_r \right)
```

Con ambos lados iguales, $\bar{S}$ colapsa en $S$ y todas las expresiones anteriores se reducen exactamente a las del caso simétrico. En cambio, el espectro **propio de cada lado**, $S_l$ o $S_r$, sí interviene en el término de primer orden de su filtro cruzado, que es el dominante y el que efectivamente cancela la diafonía: es lo que mantiene los dos filtros cruzados espectralmente distintos en una disposición asimétrica.

### Simetría de los filtros directos y convergencia

Resulta relevante destacar que los filtros FIR para las componentes directas de cada canal son idénticos —antes de aplicar el balance— pese a tratarse de un sistema asimétrico, ya que ambos dependen únicamente de $P$, que es simétrico respecto al intercambio de canales. La asimetría se refleja en los filtros FIR cruzados, que se diferencian en los factores $G_l$ y $G_r$.

La condición de convergencia en este caso asimétrico es que el módulo del producto $G_l \cdot G_r$ permanezca por debajo de la unidad en todas las frecuencias:

```math
\left | G_l(e^{j\omega}) \cdot G_r(e^{j\omega}) \right | < 1 \quad \forall \omega
```

En la parametrización habitual de NatAmbio, ambas funciones representan caminos cruzados atenuados, por lo que esta condición se cumple normalmente. Obsérvese que la condición es ahora sobre el producto y no sobre cada término por separado: la asimetría admite que uno de los dos caminos cruzados esté menos atenuado que el otro siempre que el producto siga acotado.

El factor $b$ queda fuera del bucle recursivo de cancelación y, por tanto, no modifica su condición de convergencia. Su compensación consiste en aplicar una ganancia $1/b$ tanto a la componente directa como a la cruzada destinadas al canal derecho, que es exactamente el efecto de premultiplicar $\mathbf{M}^{-1}$ por $\mathbf{D}^{-1}$. La elección del canal tomado como referencia es arbitraria: podría normalizarse igualmente respecto a $H_{rr}$ y aplicar el factor inverso al canal izquierdo.

## Ajuste del balance entre canales

En NatAmbio, la matriz $\mathbf{D}^{-1}$ no se hornea en los coeficientes generados: se realiza como una ganancia en el enrutado de convolución, que el usuario ajusta. El generador de filtros produce únicamente $\mathbf{M}^{-1}$. La razón es que el balance no es un parámetro del modelo acústico sino un ajuste de nivel del sistema concreto, que además debe hacerse a mano, midiendo o escuchando en el punto de escucha, con independencia de dónde viva el valor.

Como $\mathbf{D}^{-1}$ premultiplica a $\mathbf{M}^{-1}$, escala una fila completa de la matriz de filtrado, es decir, **todo lo que se entrega a un altavoz**: tanto su componente directa como su componente cruzada. En la práctica esto significa aplicar la misma ganancia a las dos convoluciones que alimentan la misma salida, sin importar de qué entrada proceda cada una.

### Atenuar, nunca amplificar

El factor estricto es $1/b$ sobre el canal derecho, que si $b<1$ supone un realce y, con él, un consumo de margen dinámico y riesgo de saturación. Ahora bien, multiplicar toda la matriz de filtrado por $b$ es equivalente en lo que respecta a la cancelación —solo importa el cociente entre ambas ganancias— y da $\operatorname{diag}(b,\ 1)$, es decir, una atenuación del canal izquierdo. Por tanto, la regla práctica es **atenuar el canal que llega más fuerte al punto de escucha y dejar el otro sin tocar**, nunca al revés.

### Procedimiento propuesto

El ajuste puede hacerse por escucha, sin instrumentación:

1. Reproducir una **señal mono por ambos canales**, de forma que la imagen resultante deba percibirse exactamente centrada. Sirve cualquier material monofónico, o una grabación estéreo sumada a mono.
2. Atenuar progresivamente el canal que se perciba dominante —las dos convoluciones que alimentan esa salida, con el mismo valor— hasta que la imagen se sitúe en el centro.
3. Dar el ajuste por bueno en ese punto. La resolución del oído para el centrado de una fuente fantasma mono es del orden de la décima de dB en condiciones favorables y, en cualquier caso, holgadamente mejor que el objetivo que se justifica más abajo.

Conviene señalar que si la etapa DRC ya nivela ambos canales contra un objetivo común, el balance residual es prácticamente nulo y el ajuste se queda en 0 dB. El procedimiento está para cuando no es así.

### Por qué el balance no es un ajuste cosmético

Podría pensarse que un balance mal ajustado degrada solo el equilibrio tonal o la posición de la imagen, dejando intacta la cancelación de diafonía. No es el caso. Si no se aplica $\mathbf{D}^{-1}$, el filtrado efectivo es $\mathbf{M}^{-1}$ en lugar de $\mathbf{D}^{-1}\mathbf{M}^{-1}$, y el resultado sobre el camino acústico es:

```math
\mathbf{H} \cdot \mathbf{M}^{-1} = \frac{H_{ll}}{1-P} \begin{bmatrix} 1-bP & G_r(b-1) \\ G_l(1-b) & b-P \end{bmatrix}
```

Los términos cruzados **no se anulan**: quedan proporcionales a $(b-1)$. Dicho de otro modo, el balance forma parte de la cancelación, no es un añadido posterior a ella. Esto es perfectamente coherente con lo dicho antes —$b$ está fuera del *bucle recursivo* y no afecta a la convergencia—, pero estar fuera del bucle no lo hace opcional.

### Techo de cancelación

De la expresión anterior se deduce el límite que impone un balance imperfecto. La diafonía residual relativa a la señal directa es, en primer orden, $|1-b|$ veces la que había antes de filtrar, de modo que la cancelación alcanzable queda acotada por:

```math
\text{cancelación máxima} \approx 20 \log_{10} \left| 1 - b \right|
```

| Error de balance | Techo de cancelación |
|---|---|
| 0.5 dB | ≈ −25 dB |
| 1 dB | ≈ −19 dB |
| 2 dB | ≈ −14 dB |
| 3 dB | ≈ −11 dB |
| 6 dB | ≈ −6 dB |

La lectura práctica es que **ajustar el balance con un error inferior a 1 dB sitúa el techo en unos 20 dB de cancelación**, que es del orden de lo que el propio modelo paramétrico puede entregar; afinar más allá no aporta, y quedarse en 3 dB de error limita el sistema a unos 11 dB, muy por debajo de su potencial. Es lo que convierte el "ajústalo a oído" en un objetivo con tolerancia definida, y lo que justifica que el procedimiento del apartado anterior sea suficiente.

## Protección en baja frecuencia

Como en el caso simétrico, la convergencia matemática de la serie no garantiza por sí sola un comportamiento acústico óptimo. Por debajo de unos 200 Hz la diferencia de nivel entre la recepción directa y la cruzada se reduce mucho —la cabeza deja de generar sombra acústica a longitudes de onda grandes—, de modo que $|G_l|$ y $|G_r|$ tienden a la unidad. Aunque su producto siga por debajo de 1 y la serie converja, la interacción de los filtros cruzados con la respuesta modal de la sala y la de los propios altavoces puede producir un refuerzo audible en graves, percibido no como efecto XTC sino como un realce indeseado y acústicamente resonante.

Por ello se aplica exactamente la misma protección que en el modelo simétrico: la acción XTC se acota por debajo de **200 Hz**, atenuando el nivel con una rampa de bajada de **6 dB/octava**. Está incorporada al propio modelo de $ILD_{spectrum}$, de manera que afecta por igual a $S_l$, $S_r$ y $\bar{S}$, y por tanto a los dos filtros cruzados y a los términos correctores del directo. La componente $\delta$ del filtro directo no se ve afectada, con lo que en baja frecuencia el filtro directo tiende a la unidad y el sistema converge suavemente a estéreo sin procesar.

Sobre los filtros efectivamente generados para una disposición asimétrica de ejemplo (izquierda 180 µs / 10 dB / 20°, derecha 140 µs / 8 dB / 15°, 4096 muestras a 48 kHz) se mide, en ambos filtros cruzados, una pendiente de unos 6 dB/octava por debajo de 200 Hz, mientras el filtro directo se aproxima a 0 dB. El comportamiento en graves es, por tanto, el mismo que el del caso simétrico.

## Reducción al caso simétrico

Es simple obtener, a partir de las ecuaciones del modelo asimétrico, las ecuaciones principales de la nota técnica XTC sin más que hacer $g_l = g_r = g$ y $S_l = S_r = \bar{S} = S$, con lo que $P = g^2 \ast S$ y las expresiones se reducen término a término, para un mismo $N$, a las del caso simétrico: $F^{direct} = \delta + \sum_{i=1}^{N} P^{i}$ y $F^{cross} = -G \ast \sum_{i=1}^{N} P^{i-1}$, cuyo término $i$ vale $g^{2i} \ast S^{i}$ y $g^{2i-1} \ast S^{i}$ respectivamente. Esta equivalencia es la que comprueba el test `make check` de `lib/`, que exige que el generador asimétrico reproduzca al simétrico cuando ambos lados llevan los mismos parámetros.

Aunque el modelo lo incluya, es razonable esperar que la implementación XTC en entornos asimétricos no alcance el mismo nivel de rendimiento que un sistema equivalente dispuesto simétricamente, por lo que siempre será recomendable disponer de una solución estéreo si no estándar, sí al menos simétrica.
