# Aplicación de NatAmbio XTC en entornos no simétricos

**Autor:** Raúl Fernández Ortega  
**Fecha:** julio de 2026

En la presente nota técnica se amplía el diseño de filtros XTC para entornos estéreo convencionales al caso de disposiciones en las que, por el motivo que sea, no se cumple la deseada simetría. La asimetría puede estar en la ubicación de los altavoces, pero también —y es un caso probablemente más frecuente— en el entorno acústico de una colocación por lo demás simétrica: una pared cercana a un solo lado, mobiliario distinto a izquierda y derecha. Como se verá en el apartado de validación, lo que el modelo necesita no es que la geometría sea asimétrica, sino que lo sean los caminos cruzados.

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
| $\mathbf{D}$ | Matriz diagonal de balance, $\mathbf{D} = \mathrm{diag}(1, b)$ |
| $`\mathbf{F}_{XTC}`$ | Matriz de filtrado XTC (filtros directos y cruzados) |
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

La matriz $\mathbf{H}$, puede seguir descomponiéndose en:

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

de modo que $`\mathbf{H} \cdot \mathbf{F}_{XTC} = H_{ll} \cdot \mathbf{I}`$: cada oído recibe únicamente la señal que le corresponde, con el mismo nivel en ambos, y a través de los caminos directos naturales, que permanecen intactos.

## Aplicación en NatAmbio

Como ya hemos demostrado en la nota principal sobre XTC para NatAmbio, la inversión de la matriz $\mathbf{M}$ es equivalente a su solución recursiva. Conviene recorrer el desarrollo con detalle, porque es en él donde se decide qué función $G$ acaba en cada filtro cruzado.

La inversa de una matriz $2\times2$ en el álgebra de la convolución se obtiene igual que en el caso escalar, ya que la convolución es conmutativa:

```math
\mathbf{M}^{-1} = \frac{1}{\det \mathbf{M}} \begin{bmatrix} \delta & -G_{r} \\ -G_{l} & \delta \end{bmatrix},
\qquad \det \mathbf{M} = \delta - G_{l} \ast G_{r}
```

El determinante es precisamente $\delta - P$, con $P = G_{l} \ast G_{r}$: el operador de ida y vuelta no es una definición auxiliar introducida por comodidad, sino el objeto que aparece por sí solo al invertir $\mathbf{M}$, y de ahí que la condición de convergencia recaiga sobre él y no sobre cada $G$ por separado.

Mientras $\left| P \right| < 1$, el inverso del determinante admite desarrollo en serie geométrica, que truncado a $N$ términos es:

```math
\left( \delta - P \right)^{-1} = \sum_{i=0}^{\infty} P^{i} \simeq \delta + \sum_{i=1}^{N} P^{i}
```

Sustituyendo, la matriz de filtrado queda:

```math
\mathbf{M}^{-1} \simeq \left( \delta + \sum_{i=1}^{N} P^{i} \right) \ast \begin{bmatrix} \delta & -G_{r} \\ -G_{l} & \delta \end{bmatrix}
= \begin{bmatrix} F^{direct} & F^{cross}_{l} \\ F^{cross}_{r} & F^{direct} \end{bmatrix}
```

Los dos elementos de la diagonal son el mismo desarrollo, sin factor cruzado que los distinga, y dan el filtro directo. Los dos elementos de fuera de la diagonal llevan cada uno **su propia $G$ como factor común**, heredada del cofactor correspondiente, y dan los dos filtros cruzados.

**Desarrollo detallado de un filtro cruzado.** Conviene recorrer uno de ellos paso a paso, porque es donde resulta fácil perder la pista de los índices. Se toma el elemento $(1,2)$ de $\mathbf{M}^{-1}$, que según la correspondencia del apartado siguiente es el que alimenta al altavoz izquierdo:

```math
F^{cross}_{l} = \frac{1}{\det \mathbf{M}} \ast \left( -G_{r} \right) = \left( \delta - P \right)^{-1} \ast \left( -G_{r} \right)
```

Sustituyendo el determinante por su desarrollo en serie:

```math
F^{cross}_{l} = -G_{r} \ast \sum_{i=0}^{\infty} P^{i}
```

Truncando la serie —un orden antes que el directo, por el criterio que se justifica más abajo— y escribiendo los primeros sumandos:

```math
F^{cross}_{l} \simeq -G_{r} \ast \left( \delta + P + P^{2} + \dots + P^{N-1} \right)
```

**Aquí es donde aparece el $\delta$**, y es el único sitio donde se ve escrito: es el término $P^{0}$ de la serie, el mismo que en el filtro directo se saca fuera del sumatorio. Reindexando ahora para que el sumatorio empiece en $i=1$ —de modo que el índice numere el mismo escalón que en el directo— el exponente pasa a ser $i-1$ y el $\delta$ queda absorbido como su primer sumando:

```math
F^{cross}_{l} = -G_{r} \ast \sum_{i=1}^{N} P^{i-1}
```

Sustituyendo $P = G_{l} \ast G_{r}$ en cada sumando:

```math
F^{cross}_{l} = -G_{r} \ast \delta - G_{r} \ast \left( G_{l} \ast G_{r} \right) - G_{r} \ast \left( G_{l} \ast G_{r} \right)^{2} - \dots
```

es decir, agrupando potencias:

```math
F^{cross}_{l} = -G_{r} - G_{l} \ast G_{r}^{2} - G_{l}^{2} \ast G_{r}^{3} - \dots
```

de donde se lee el término general, con $G_{r}$ siempre un orden por delante de $G_{l}$:

```math
F^{cross}_{l} = - \sum_{i=1}^{N} G_{r}^{i} \ast G_{l}^{i-1}
```

Y deshaciendo la normalización, con $G_{l} = H_{lr}/H_{ll}$ y $G_{r} = H_{rl}/H_{rr}$:

```math
F^{cross}_{l} = - \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i-1}} {H_{rr}^{i} \ast H_{ll}^{i-1}}
```

El otro cruzado sale del elemento $(2,1)$, que lleva $-G_{l}$ en lugar de $-G_{r}$, y su desarrollo es idéntico intercambiando los papeles de $G_{l}$ y $G_{r}$.

Recogiendo los cuatro elementos, y escritos también en función de los caminos acústicos:

```math
F^{direct}_{l} = \delta + \sum_{i=1}^{N} \frac {H_{lr}^{i} \ast H_{rl}^{i}} {H_{ll}^{i} \ast H_{rr}^{i}} = \delta + \sum_{i=1}^{N} G_{l}^{i} \ast G_{r}^{i} = \delta + \sum_{i=1}^{N} \left( G_{l} \ast G_{r} \right)^{i}
```

```math
F^{direct}_{r} = \delta + \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i}} {H_{rr}^{i} \ast H_{ll}^{i}} = \delta + \sum_{i=1}^{N} G_{r}^{i} \ast G_{l}^{i} = \delta + \sum_{i=1}^{N} \left( G_{r} \ast G_{l} \right)^{i}
```

```math
F^{cross}_{l} = - \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i-1}} {H_{rr}^{i} \ast H_{ll}^{i-1}} = - \sum_{i=1}^{N} G_{r}^{i} \ast G_{l}^{i-1} = - G_{r} \ast \sum_{i=1}^{N} \left( G_{l} \ast G_{r} \right)^{i-1}
```

```math
F^{cross}_{r} = - \sum_{i=1}^{N} \frac {H_{lr}^{i} \ast H_{rl}^{i-1}} {H_{ll}^{i} \ast H_{rr}^{i-1}} = - \sum_{i=1}^{N} G_{l}^{i} \ast G_{r}^{i-1} = - G_{l} \ast \sum_{i=1}^{N} \left( G_{r} \ast G_{l} \right)^{i-1}
```

Los dos filtros directos son iguales por conmutatividad de la convolución, $\left( G_{l} \ast G_{r} \right)^{i} = \left( G_{r} \ast G_{l} \right)^{i}$, y en adelante se denotan sin subíndice. Nótese que **en los cruzados los exponentes del denominador van cruzados respecto a los del numerador**: en $`F^{cross}_{l}`$ el camino $H_{rl}$ aparece elevado a $i$ y normalizado por $H_{rr}^{i}$ —esto es, forma $G_{r}^{i}$—, mientras el otro par queda a $i-1$. Es un punto fácil de equivocar, y la comprobación es el término de primer orden: con $i=1$ el filtro debe reducirse a $-G_{r} = -H_{rl}/H_{rr}$, y no a $-H_{rl}/H_{ll}$, que sería $-b \ast G_{r}$ e introduciría el balance dentro de $\mathbf{M}^{-1}$, precisamente donde el modelo no lo quiere.

**Relación con las expresiones de la nota principal.** [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md#an%C3%A1lisis-del-problema-y-resoluci%C3%B3n) escribe estos mismos cuatro filtros en su forma completa, con el balance incluido de manera implícita, ya que allí la cancelación se resuelve directamente sobre los caminos acústicos sin factorizar $\mathbf{D}$. Sus filtros cruzados quedan normalizados por el camino directo del altavoz que **radia** la antiseñal —allí $`F^{cross}_{r}`$ evaluado en $i=1$ vale $-H_{lr}/H_{rr}$—, mientras que aquí los elementos de $\mathbf{M}^{-1}$ normalizan cada $G$ por el camino directo de su **propio** altavoz —aquí $-G_{l} = -H_{lr}/H_{ll}$—. Las dos expresiones difieren exactamente en el factor $b$ que $\mathbf{D}^{-1}$ aporta después, de modo que describen el mismo filtrado; bajo la hipótesis de simetría de la nota principal, $b = \delta$ y coinciden término a término. Al comparar ambas notas conviene tener presente cuál de las dos normalizaciones se está leyendo, porque los subíndices de los denominadores no son los mismos.

El criterio de truncamiento es que ningún filtro exceda la extensión temporal $N(\tau_{l} + \tau_{r})$ del directo: como el factor $G$ ya aporta un medio escalón de retardo, la serie que acompaña al cruzado se corta un orden antes, con lo que su último tap cae en $(N-1)(\tau_{l}+\tau_{r}) + \tau_{x}$ y queda dentro de esa misma extensión. Es también lo que la recurrencia de Horner entrega de forma natural, sin cálculo adicional.

En forma compacta, con $P$ como operador de ida y vuelta:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^i
```

```math
F^{cross}_l = - {G_{r}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{r}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

```math
F^{cross}_r = - {G_{l}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{l}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

El operador de ida y vuelta que ha aparecido como determinante admite una lectura acústica directa:

```math
P = G_{l} \ast G_{r}
```

es exactamente el trayecto que describe un escalón de la recursión: la señal sale del altavoz izquierdo hacia el oído derecho —factor $G_{l}$— y vuelve desde el altavoz derecho hacia el oído izquierdo —factor $G_{r}$—. Es también el producto sobre el que se establece la condición de convergencia, y el objeto simétrico bajo intercambio de canales del que dependen los dos filtros directos.

### Correspondencia entre filtros, entradas y altavoces

Llamando $\mathbf{x} = (x_{l}, x_{r})$ al par de señales de programa y $\mathbf{s} = (s_{l}, s_{r})$ al par entregado a los altavoces, se tiene $`\mathbf{s} = \mathbf{F}_{XTC} \ast \mathbf{x}`$, de modo que **cada fila de la matriz de filtrado corresponde a un altavoz** y cada columna a una entrada:

```math
s_{l} = F^{direct} \ast x_{l} + F^{cross}_{l} \ast x_{r}
```

```math
s_{r} = \frac{1}{b} \left( F^{direct} \ast x_{r} + F^{cross}_{r} \ast x_{l} \right)
```

| Filtro | Alimenta al altavoz | Toma la entrada | Contiene | Primer tap en | Nivel del primer tap |
|---|---|---|---|---|---|
| $`F^{cross}_{l}`$ | izquierdo | derecha | $G_{r}$ | $`\text{ITD}_{r}`$ | $`-\text{ILD}_{r}`$ |
| $`F^{cross}_{r}`$ | derecho | izquierda | $G_{l}$ | $`\text{ITD}_{l}`$ | $`-\text{ILD}_{l}`$ |

Todo en la rama cruzada izquierda es "derecho" —la entrada, la función $G$, el ITD y el ILD— excepto el altavoz que la radia. La razón es física y no una convención de escritura: la fuga que hay que cancelar en el oído izquierdo es la del altavoz **derecho**, descrita por $G_{r}$; pero la antiseñal que la cancela tiene que llegar a ese mismo oído izquierdo, y el único camino directo que llega allí es el del altavoz **izquierdo**. De ahí el cruce: el subíndice del filtro nombra el altavoz que radia, el subíndice de la $G$ que contiene nombra el altavoz cuya fuga cancela, y son siempre opuestos.

Puede comprobarse sobre la contribución al oído izquierdo de la señal $x_{r}$, que es la que no debería llegar allí. El altavoz derecho aporta $s_{r}$ a través de su camino cruzado $H_{rl}$ y el izquierdo aporta $s_{l}$ a través de su camino directo $H_{ll}$:

```math
e_{l} \big|_{x_{r}} = \frac{x_{r}}{\delta - P} \ast \left( \frac{H_{rl}}{b} - G_{r} \ast H_{ll} \right) = 0
```

ya que $G_{r} \ast H_{ll} = \left( H_{rl}/H_{rr} \right) \ast H_{ll} = H_{rl}/b$. La cancelación es exacta, y lo es **solo con el factor $1/b$ en su sitio**: es el mismo resultado del apartado sobre el ajuste del balance, visto término a término en lugar de matricialmente.

### Descomposición del operador de ida y vuelta

La expresión $P = G_{l} \ast G_{r}$ es la que da sentido al desarrollo, pero no es la que se lleva directamente a código. 

Recordemos que según se hizo en el planteamiento de [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md#desarrollo-del-dise%C3%B1o-final), tanto $G_{l}$ como $G_{r}$ se pueden expresar como dependientes de:

```math
G_{l} = \delta \left ( \text{ITD}_{l}, \text{ILD}_{l} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{l} \right), \text{ILD}_{avg} \left ( \Theta_{l} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{l}, f \right )
```

```math
G_{r} = \delta \left ( \text{ITD}_{r}, \text{ILD}_{r} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{r} \right), \text{ILD}_{avg} \left ( \Theta_{r} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{r}, f \right )
```

La parametrización de cada una de estas funciones puede seguir el mismo modelo desarrollado en la nota principal XTC para NatAmbio, de manera que el paso a implementación para cada función $G$ se separa en su parte de banda ancha y su forma espectral:

$$G_x = g_x \ast S_x$$

$$g_x = \delta \left ( \text{ITD} \left ( \Theta_{x} \right ), \text{ILD}_{avg} \left ( \Theta_{x} \right ) \right)$$

$$S_x = \text{ILD}_{spectrum} \left ( \Theta_{x}, f \right )$$

y entra en juego el convenio establecido en la nota principal —[aplicación del espectro ILD en la serie](xtc_filters_es.md#aplicaci%C3%B3n-del-espectro-ild-en-la-serie)—: el espectro se aplica una sola vez por escalón, mientras que el retardo y la atenuación de banda ancha sí siguen la ley completa de potencias. El operador de ida y vuelta se genera entonces como

```math
P = g_l \ast g_r \ast \bar{S}
```

esto es: los retardos de ambos lados se suman, las atenuaciones se multiplican, y allí donde el producto $G_{l} \ast G_{r}$ acumularía $S_l \ast S_r$ el espectro interviene una única vez, a través de una forma promedio $\bar{S}$. Con ello el término $i$ del filtro directo vale: 
```math
(g_l g_r)^i \ast \bar{S}^i
```
y el término $i$ de cada cruzado:
```math
g_r (g_l g_r)^{i-1} \ast S_r \ast \bar{S}^{i-1}
``` 
de modo que el número de aplicaciones del espectro es $i$ en todos los casos, con el mismo índice $i$ numerando el mismo escalón en el filtro directo y en los cruzados.

Queda por determinar qué forma espectral corresponde a $\bar{S}$. Como la ida y vuelta atraviesa una sombra acústica de cabeza por cada lado, y solo hay sitio para una aplicación, la elección natural es la media de ambas log-magnitudes. Dado que el modelo empírico de $ILD_{spectrum}$: 

```math
ILD_{spectrum}(f) = \alpha \cdot 10 \cdot log_{10}(f/1000 + 1) \cdot \sin(\Theta)
```

es lineal en el producto $\alpha \sin\Theta$, esa media se obtiene sin promediar respuestas: basta evaluar el mismo modelo con

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

El factor estricto es $1/b$ sobre el canal derecho, que si $b<1$ supone un realce y, con él, un consumo de margen dinámico y riesgo de saturación. Ahora bien, multiplicar toda la matriz de filtrado por $b$ es equivalente en lo que respecta a la cancelación —solo importa el cociente entre ambas ganancias— y da $\mathrm{diag}(b, 1)$, es decir, una atenuación del canal izquierdo. Por tanto, la regla práctica es **atenuar el canal que llega más fuerte al punto de escucha y dejar el otro sin tocar**, nunca al revés.

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

Los términos cruzados **no se anulan**: quedan proporcionales a $(b-1)$. Dicho de otro modo, el balance forma parte de la cancelación, no es un añadido posterior a ella. Esto es perfectamente coherente con lo dicho antes —el balance $b$ está fuera del *bucle recursivo* y no afecta a la convergencia—, pero estar fuera del bucle no lo hace opcional.

### Techo de cancelación

De la expresión anterior se deduce el límite que impone un balance imperfecto. La diafonía residual relativa a la señal directa es, en primer orden, $|1-b|$ veces la que había antes de filtrar, de modo que la cancelación alcanzable queda acotada por:

```math
\text{cancelacion}_{max} \approx 20 \log_{10} \left| 1 - b \right| \ \text{dB}
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

## Validación por escucha

El modelo se ha contrastado sobre un sistema real, con un resultado que conviene documentar tanto por lo que confirma como por lo que reencuadra.

### El caso

Altavoces en disposición simétrica —mismo ITD y mismo azimut en ambos lados— pero con los dos caminos cruzados netamente distintos: el ajuste de oído converge a $\text{ILD}_l = 21$ dB y $\text{ILD}_r = 12$ dB, nueve decibelios de diferencia. El balance resultante del procedimiento descrito más arriba es de 0 dB; con solo 1 dB de atenuación en el canal derecho la imagen mono ya se desplazaba perceptiblemente hacia la izquierda.

### El balance nulo es coherente con el modelo

$b = H_{rr}/H_{ll}$ es el cociente de los caminos **directos**, mientras que los ILD parametrizan los **cruzados**: son magnitudes independientes. Con la etapa DRC nivelando ambos directos contra un objetivo común, $b \approx 1$ es lo esperable por mucho que la diafonía sea distinta a cada lado. En este sistema la asimetría vive íntegramente en $\mathbf{M}$ y nada en $\mathbf{D}$, que es el caso límite opuesto al que motiva el apartado del balance. De paso, la sensibilidad observada —1 dB claramente audible sobre imagen mono— confirma que el procedimiento de escucha resuelve con holgura el objetivo de tolerancia que justifica la tabla del techo de cancelación.

### Por qué el compromiso simétrico salía caro

Antes de disponer del modelo asimétrico el sistema funcionaba con un único ILD de 16 dB, valor de compromiso. Con 12 dB en ambos lados uno de ellos se comportaba de forma excelente —la escena virtual abría más allá de 70° de azimut— mientras el otro colapsaba, sin llegar a abrir 40°.

La observación relevante es que **el fallo no es simétrico**. Quedarse corto en la cancelación se limita a estrechar la escena; pasarse la destruye, porque la señal correctora excede a la diafonía real y el residuo, invertido y desplazado en el tiempo, introduce una pista de localización que no corresponde a ninguna fuente. Un parámetro único queda por tanto sujeto al peor de los dos lados: no puede ser tan agresivo como admite el lado bueno sin romper el contrario. El compromiso a 16 dB no repartía el error a partes iguales, sino que renunciaba a la mayor parte de la anchura alcanzable en un lado para evitar el colapso en el otro. Con los dos ILD independientes la escena resultó no solo más amplia sino **estable**, que es la firma de una cancelación bien ajustada en tiempo y en nivel.

Este es, en la práctica, el argumento más fuerte a favor del modelo asimétrico, y es independiente del balance: aun con $b = 1$ exacto, un único $G$ obliga a tirar por lo bajo.

### Origen probable y techo asociado

La hipótesis del oyente, coherente con la geometría de su sala, es que la diferencia procede de las reflexiones tempranas, distintas a izquierda y derecha por los límites del recinto y el mobiliario. El mecanismo es plausible por una razón de margen: el camino directo es la llegada más fuerte al oído ipsilateral y una reflexión varios decibelios por debajo apenas lo perturba, mientras que el camino cruzado llega ya atenuado por la sombra de la cabeza, de modo que una reflexión que alcanza el oído contralateral sin sufrir esa sombra puede rivalizar con él en nivel. La misma asimetría de sala altera $G_l$ y $G_r$ mucho más de lo que altera $H_{ll}$ y $H_{rr}$ —y lo poco que altera a estos últimos lo corrige el DRC.

Ello acota lo que cabe esperar. Una reflexión es un fenómeno temporal y el parámetro $ILD_{avg}$ solo puede absorberla como nivel: el modelo cancela una única copia retardada y no puede cancelar una segunda llegada a un retardo distinto. El valor al que converge el ajuste de oído es entonces un compromiso entre cancelar la diafonía directa y no empeorar el residuo frente a la reflexión, y el techo que ello impone no está en los parámetros sino en la sala. Congruentemente, la mejora reportada frente al ajuste simétrico de compromiso se describe como real pero moderada: en un sistema así, el siguiente escalón de mejora no es afinar el XTC sino tratar el punto de primera reflexión.

### Alcance de esta validación

Se trata de un único sistema y de un ajuste subjetivo, sin verificación instrumental de la hipótesis de las reflexiones —que se confirmaría midiendo la respuesta binaural en el punto de escucha y comparando las primeras llegadas de cada camino cruzado. Lo que sí queda establecido es que el modelo asimétrico cubre un caso de uso que la motivación original de esta nota no contemplaba: no la asimetría de colocación, sino la de entorno acústico con los altavoces colocados simétricamente.

## Reducción al caso simétrico

Es simple obtener, a partir de las ecuaciones del modelo asimétrico, las ecuaciones principales de la nota técnica XTC sin más que hacer:

```math
g_l = g_r = g
```

y

```math
S_l = S_r = \bar{S} = S
```

con lo que

```math
P = g^2 \ast S
```

y las expresiones se reducen término a término, para un mismo $N$, a las del caso simétrico:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^{i}
```

y

```math
F^{cross} = -G \ast \sum_{i=1}^{N} P^{i-1}
```

cuyo término $i$ vale

```math
g^{2i} \ast S^{i}
```

y

```math
g^{2i-1} \ast S^{i}
```

respectivamente. Esta equivalencia es la que comprueba el test `make check` de `lib/`, que exige que el generador asimétrico reproduzca al simétrico cuando ambos lados llevan los mismos parámetros.

Aunque el modelo lo incluya, es razonable esperar que la implementación XTC en entornos asimétricos no alcance el mismo nivel de rendimiento que un sistema equivalente dispuesto simétricamente, por lo que siempre será recomendable disponer de una solución estéreo si no estándar, sí al menos simétrica.
