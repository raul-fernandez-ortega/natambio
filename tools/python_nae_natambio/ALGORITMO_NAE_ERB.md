# NAE_ERB: el algoritmo

> Las fórmulas van en LaTeX. Se componen en la vista previa de VS Code
> (extensión **Markdown Math**, integrada) y en GitHub. En un visor que no la
> soporte se verán las marcas de LaTeX a la vista, sin componer.

**Borrador de trabajo, septiembre de 2026.** Describe lo que hace el motor de
tiempo real (`src/nae_erb.cpp` / `.hpp`) y marca en qué se aparta de él el
script de estudio (`nae_natambio_erb.py`). No es todavía documentación para
publicar.

Los números concretos que aparecen son los de la configuración de producción de
panambio01: 48 kHz, periodo de 256 muestras, `steps_length` 3, `cov_window_ms`
64, `delta_erb` 2, `band_min_hz` 125 — que dan 20 bandas.

---

## 1. De qué parte

NAE descompone un par estéreo en dos componentes. Se forma el par mid/side

$$
m(t) = L + R \qquad\qquad s(t) = w\,(L - R)
$$

se estima la covarianza 2×2 de `(m, s)` sobre una ventana, y se diagonaliza. El
autovector principal **v₁** es la dirección en la que la señal tiene más
energía; **C1** es la proyección sobre ella y **C2** lo que queda.

Perceptualmente: en una grabación con una imagen definida, `m` y `s` están
correlacionados de una forma que apunta a dónde está la fuente dominante. C1 es
esa fuente; C2 es lo que no se explica por ella — reverberación, ambiente,
fuentes secundarias descorrelacionadas.

**El motor de banda ancha hace eso una vez para todo el espectro.** Un solo eje
para toda la señal. Si suena un contrabajo al centro y unos platos a la derecha,
el PCA encuentra un único compromiso entre los dos, y lo que sale de C2 lleva
residuos del uno y del otro.

**NAE_ERB hace lo mismo pero por bandas.** Un eje por banda crítica, veinte ejes
en vez de uno. El contrabajo domina su banda, los platos la suya, y cada una se
descompone con su propia dirección.

---

## 2. La idea central: el banco desaparece

Lo que uno esperaría es: filtrar la señal en 20 bandas, correr un PCA en cada
una, y sumar las 20 salidas. Eso son 20 filtrados, 20 PCA y 20 reconstrucciones
por bloque, con la latencia de los filtros encima. **No es lo que se hace, y no
hace falta.** Dos identidades borran el banco del camino de audio.

### 2.0 Qué es $W_b(f)$, que todo lo demás se apoya en ello

La **máscara** de la banda $b$: un peso entre 0 y 1 por cada pareja (banda, bin
de la FFT), calculado una vez al cargar y nunca más. Sale en dos pasos.

Primero la forma, una magnitud gammatone de orden $n = 4$ centrada en $f_b$ con
ancho $B_b$:

$$
A_b(f) \;=\; \Bigl[\,1 + \Bigl(\frac{f - f_b}{B_b}\Bigr)^{2}\,\Bigr]^{-n/2}
$$

y después la normalización punto a punto, que es la que importa:

$$
W_b(f) \;=\; \frac{A_b(f)}{\sum_j A_j(f)}
\qquad\Longrightarrow\qquad
\sum_b W_b(f) = 1 \ \ \text{en cada bin}
$$

$f_k = k\,f_s/N$ es la frecuencia del bin $k$, así que $W_b(f_k)$ es «cuánto de
este bin pertenece a la banda $b$». Con el banco de producción, 20 bandas y bins
de 15,625 Hz:

| bin | $f_k$ | bandas con peso apreciable (centro: peso) | suma |
|---|---|---|---|
| 8 | 125 Hz | 20: 0,226 · 145: 0,626 · 270: 0,120 · 395: 0,021 | 1,000000000000 |
| 32 | 500 Hz | 270: 0,041 · 395: 0,273 · 545: 0,623 · 731: 0,041 | 1,000000000000 |
| 64 | 1000 Hz | 731: 0,032 · **961: 0,850** · 1247: 0,092 · 1601: 0,010 | 1,000000000000 |
| 128 | 2000 Hz | 1601: 0,039 · **2040: 0,901** · 2585: 0,045 | 1,000000000000 |
| 800 | 12500 Hz | 10005: 0,026 · **12462: 0,909** · 15509: 0,056 | 1,000000000000 |

De dónde salen los centros $f_b$ y los anchos $B_b$ lo cuenta la sección 4.
Aquí hacen falta sólo dos propiedades, y la segunda es la que cuesta ver:

- $\sum_b W_b(f) = 1$ en cada bin. El banco reconstruye **por construcción**, no
  por aproximación.
- **No existe ninguna señal filtrada de la banda $b$.** La banda *es* esa curva
  de pesos. «La señal de la banda $b$» sería $W_b(f)X(f)$, y en este algoritmo
  **nunca se calcula**: aparece sólo como paso intermedio para deducir las dos
  identidades de abajo, y desaparece en el resultado.

De ahí salen las dos tablas que guarda el motor, y por qué son dos:

- la **covarianza** pesa con $W_b^2$, porque la energía de la banda es
  $\sum_k |W_b X_k|^2 = \sum_k W_b^2\,|X_k|^2$;
- la **síntesis** pesa con $W_b$, porque la aportación de la banda a la salida
  es $W_b P_b X$, lineal en la máscara.

En `nae_erb.hpp` son `masks` y `masks2`, de `n_bins × n_bands` cada una —1537 ×
20 aquí, unos 246 KB— guardadas **bin-major**, `[k*n_bands + b]`, porque los dos
bucles que las leen recorren cada bin y en él todas las bandas. Guardarlas
banda-major costaba tres veces más.

### 2.1 La covarianza sale del espectro (Parseval)

La covarianza de la banda *b* sobre la ventana es, por Parseval, el espectro de
la ventana pesado por $W_b(f)^2$:

$$
R_b \;=\; \sum_k W_b(f_k)^2
\begin{pmatrix}
|M_k|^2 & \operatorname{Re}(M_k S_k^{*}) \\
\operatorname{Re}(M_k S_k^{*}) & |S_k|^2
\end{pmatrix}
$$

Es decir: **una sola transformada de la ventana**, y de ella salen las veinte
covarianzas como veinte sumas pesadas distintas. Ni un filtro, ni una
transformada inversa, ni un retardo.

### 2.2 Los proyectores se suman antes de aplicarse

Cada banda reconstruye aplicando un proyector 2×2 constante $P_b = v_b v_b^{\mathsf{T}}$ a
la señal de esa banda, $W_b(f)\,X(f)$. La suma sobre bandas es entonces

$$
C_1(f) \;=\; \Bigl[\, \sum_b W_b(f)\, P_{1,b} \,\Bigr] X(f) \;=\; G_1(f)\, X(f)
$$

**Una sola matriz 2×2 dependiente de frecuencia**, tenga el banco diez bandas o
cuarenta. Y como las máscaras suman uno en cada bin ($\sum_b W_b = 1$) y los
proyectores de un par ortonormal suman la identidad ($P_{1,b} + P_{2,b} = I$),
resulta $G_1 + G_2 = I$.

### 2.3 Lo que eso cuesta

**El número de bandas no entra en el número de transformadas.** Por periodo:

```
    2 transformadas directas       (mid y side, sobre n_cov = 3072)
    1 suma pesada y 1 problema propio 2×2 por banda   (20 veces, barato)
    ensamblado de G(f)             (n_bins × n_bands)
    2 transformadas inversas       (C1 mid y C1 side)
```

**El número de bandas no entra en el número de transformadas.** Pero sí entra en
el total, y no poco: los dos bucles de banda —acumular covarianzas y ensamblar
$G$— son ambos $O(n_{\text{bins}} \times n_{\text{bandas}})$, y con tres mil bins
y cuarenta bandas pesan tanto como las transformadas. Medido con `nae_bench` a
48 kHz, periodo 256, `covsteps` 3:

| bandas | ventana | µs/bloque | % del periodo |
|---|---|---|---|
| 18 | 32 ms | 242 | 4,5 % |
| 20 | 64 ms | 516 | 9,7 % |
| 36 | 64 ms | 711 | 13,3 % |
| 21 | 128 ms | 1082 | 20,3 % |
| 40 | 128 ms | 1634 | 30,6 % |

A ventana fija, doblar las bandas cuesta entre un 38 y un 51 %. Lo que compra la
formulación es que **no** sea un factor de dos por banda; lo que no compra es un
banco gratis.

---

## 3. Las tres ventanas

Esta es la parte que más cuesta de ver, porque son tres cosas distintas que en
el motor de banda ancha eran una sola.

Las tres **terminan en la muestra que acaba de llegar** y se extienden hacia
atrás.

### 3.1 Ventana de reconstrucción — `n_pca = covsteps × 256 = 768` (16 ms)

Es la de NAE de toda la vida: el solapamiento de la suma-solapada, y **la que
fija la latencia**. No se toca. Con `steps_length 3` son 768 muestras, 16,0 ms,
y eso es exactamente lo que natambio declara ahora a JACK (más los 64 del XTC).

### 3.2 Ventana de análisis — `n_cov`, de `<cov_window_ms>` (64 ms → 3072)

Sobre cuántas muestras del pasado se estima la covarianza. **No cuesta latencia
ninguna**: son muestras ya reproducidas, se mira hacia atrás.

Es lo que hace que el eje esté quieto. Medido fuera de línea sobre *I Am In
Love*, con el mismo banco y el mismo suelo de 125 Hz, el movimiento mediano del
eje entre bloques baja de **9,65° con 16 ms a 3,65° con 64 ms** — que es
prácticamente el 3,34° del motor de banda ancha.

Un eje que bailotea se oye como imagen inestable. Por eso existe esta ventana.

### 3.3 Ventana de síntesis

Un producto en frecuencia es una **convolución circular**. Si $G_1(f)$ se aplica
sobre exactamente las 768 muestras de la reconstrucción, no hay holgura: la cola
de la ventana se envuelve sobre su cabeza — y la cabeza es justo la trama que se
va a emitir.

Lo pérfido es que **ese envolvimiento se cancela entre C1 y C2**, porque
$G_1 + G_2 = I$. Cualquier control que verifique `C1 + C2 = entrada` lo da por
bueno. Pero dentro de C1 o de C2 por separado es aliasing de banda ancha, y
contra una componente cuyo espectro cae con la frecuencia acaba **por encima**
de la señal: medido, estaba 7,9 dB **sobre** C1 entre 10 y 20 kHz sin holgura, y
60 dB por debajo con ella. *Se oyó antes de medirse.*

En el motor de C++ la ventana de síntesis **es la misma que la de análisis**
(`n_cov`), y la trama que se emite se lee en `syn_offset = n_cov − n_pca = 2304`,
con 768 muestras de contexto a su derecha y 2304 a su izquierda. Esa holgura es
lo que impide el envolvimiento.

> **Ojo al comparar con el script.** Ahí la ventana de síntesis es independiente
> (`--syn-window`) y **por defecto vale `2 × n_pca`**, o sea 1536 muestras con
> esta configuración, la mitad de las 3072 del motor. Una tirada del script sin
> `--syn-window 3072` no tiene la misma holgura que producción y su aliasing de
> síntesis no es el mismo. Es la diferencia más fácil de pasar por alto de toda
> esta lista.

---

### 3.4 El banco viaja gratis dentro de la ventana

Lo natural es esperar que un banco de filtros cueste latencia, y en la
formulación literal la cuesta: convertir las máscaras en FIR obliga a darles a
todas la misma fase lineal, con un retardo de grupo común $T_0 = \texttt{nfft}/2$
que hay que pagar y luego descontar. El script lo hace así en `--filtering whole`
y `block`, y por eso su control de reconstrucción compara $x(t)$ y no $x(t-T_0)$:
le quita el retardo antes de comparar.

**En el motor no existe ningún kernel.** Las máscaras nunca se convierten en una
respuesta impulsional. Se usan como dos cosas y sólo dos:

- `masks2`, pesos reales no negativos sumando espectros de **potencia** — ahí no
  interviene la fase en absoluto;
- `masks`, pesos reales que construyen $G_1(f)$, que se aplica como multiplicador:

```c
prod[0][k][0] = gcoef[0][k] * mr + gcoef[1][k] * sr;   /* parte real      */
prod[0][k][1] = gcoef[0][k] * mi + gcoef[1][k] * si;   /* parte imaginaria */
```

Un multiplicador **real** sobre las dos partes es un filtro de **fase cero**:
respuesta impulsional simétrica en torno a $n = 0$ y **retardo de grupo nulo en
toda frecuencia**. El banco no cuesta ni una muestra.

**Y aquí está el detalle fino.** Un filtro de fase cero tiene que mirar hacia
delante, lo cual normalmente es imposible. Pero la trama que se emite **no es la
última de la ventana**: se lee en `syn_offset`, con $n_{\text{cov}} - n_{\text{pca}}$
muestras de contexto a su izquierda y $n_{\text{pca}} = 768$ **a su derecha**. El
alcance hacia delante del banco cae dentro de muestras que, respecto a la trama
emitida, ya han llegado.

Es decir: **la ventana de reconstrucción es a la vez lo que la suma-solapada
exige y la holgura que el banco necesita.** El banco viaja gratis dentro de una
latencia que NAE ya estaba pagando por otro motivo. No es un ahorro que se haya
buscado; es un efecto lateral del ventaneo, y de los que salen bien.

Medido, sin teoría de por medio:

```
   latencia medida por correlación cruzada:   833 muestras
   ventana de reconstrucción (covsteps x 256): 768
   retardo del modelo XTC:                      64
                                               ---
                                               832
```

No queda sitio para un retardo de banco. Si las máscaras llevasen $T_0$, ahí
habría varios miles de muestras más.

**Lo que sí cuesta, y no es latencia.** Una banda de ancho $B$ reparte cada
muestra sobre unos $1/B$ segundos, se formule como se formule. Con el suelo en
125 Hz son 8 ms, y el emborronamiento que mide el script da 8,3 ms de máximo.
La diferencia no está en la anchura sino en **dónde cae**: desplazada $T_0$ hacia
el futuro en los modos FIR, y por tanto latencia; simétrica alrededor de la
propia muestra en el motor, y por tanto gratis.

---

## 4. El banco

Filtros tipo gammatone de orden 4, anchura de una ERB, con **suelo**:

$$
B_b \;=\; \max\bigl(\, 1{,}019 \cdot \mathrm{ERB}(f_b),\;\; B_{\min} \,\bigr)
$$

donde $B_{\min}$ es `<band_min_hz>`. Y los centros avanzan `delta_erb` unidades
en la escala ERB **o** $B_{\min}$ en Hz, lo que sea mayor.

**Por qué el suelo.** Una ventana de N muestras no resuelve más fino que $f_s/N$,
se construya el banco como se construya. A 64 ms eso son 15,6 Hz. Con el suelo
en 125 Hz, cada banda del suelo abarca 8 bins — holgado. Con una ventana de
16 ms serían 2 bins, y con dos bins no hay covarianza que estimar que valga.

**Por qué los centros también se separan.** Centros más juntos que el ancho de
las bandas serían casi duplicados: un PCA cada uno y la misma información entre
ellos que la que llevaría uno solo.

Con `delta_erb 2` y suelo de 125 Hz salen **20 bandas**, de 20 Hz a 19287 Hz.
Las seis primeras caen en el suelo (ancho 125 Hz, centros cada 125 Hz); a partir
de 961 Hz manda la ERB y las bandas se ensanchan.

**Normalización punto a punto** (la de la sección 2.0). Es lo que hace que las
máscaras sumen uno en cada bin y el banco sea de reconstrucción perfecta *por
construcción*. Normalizar cada filtro a energía unidad **no**
serviría: $\sum_b |H_b|^2 = 1$ no implica $\sum_b H_b = 1$, y es la segunda la que hace
falta.

El precio: las bandas de los extremos dejan de tener forma de gammatone —por
debajo del centro más bajo y por encima del más alto no hay con quién repartirse
el bin, así que se lo quedan entero. Por eso el rango cubierto se pone fuera de
la región de interés (20 Hz a 0,95·Nyquist).

---

## 5. El paso, en orden

Lo que hace `NaeErb::decompose()` en cada periodo de 256 muestras:

**1. Entra la trama.** Se aplica la matriz de anchura (`pan_scale`) recorriendo
la rampa, se forma `mid = L+R` y `side = w·(L−R)`, y se escriben al final del
historial de 3072 muestras. El `side_weight` se escribe **sólo en esta trama**:
las viejas conservan el peso con el que entraron.

**2. Dos transformadas directas** de mid y side sobre las 3072 muestras.

**3. Los tres productos espectrales**, plegados a la suma de Parseval completa:
`fold[k]` vale 2 salvo en DC —donde vale **0**, que es lo que quita la media— y
en Nyquist, donde vale 1.

**4. Una covarianza por banda**: `Σ_k W_b(f_k)² · producto[k]`, normalizada por
$1/\bigl(N(N-1)\bigr)$ — la misma que usa `np.cov` y la misma que el motor de
banda ancha.

**5. Veinte problemas propios 2×2**, ordenados por autovalor. Se guarda **sólo
el proyector principal** $P_{1,b} = v_1 v_1^{\mathsf{T}}$, tres números por banda (mm, ms, ss),
porque $P_2 = I - P_1$.

**6. El anillo.** Esos tres números por banda van a la posición `ring_pos` de un
anillo de `covsteps = 3` ranuras. Se suman las tres ranuras.

> **Por qué sumar el anillo es la suma-solapada.** Aplicar un filtro es lineal
> en el filtro: sumar los filtros de las tres últimas ventanas y aplicar una vez
> da lo mismo que aplicar cada uno y sumar, que es lo que hace la suma-solapada.
> Y hacerlo así permite que la síntesis ocurra **en el momento de la emisión**,
> cuando la trama tiene contexto a los dos lados. El anillo guarda coeficientes
> por banda, no arrays por bin, porque $\sum_k W^{\mathsf{T}} p_k = W^{\mathsf{T}} \sum_k p_k$.

**7. Se ensambla $G_1(f)$**: para cada bin, $\sum_b W_b(f)\, s_b$. El anillo se
suma **antes** de tocar las máscaras; hacerlo al revés recorría el array de
máscaras tres veces y costaba tres cuartas partes del bloque.

**8. Se aplica y se vuelve al tiempo**: dos inversas, C1 mid y C1 side.

**9. C2 por resta.** Como $G_1 + G_2 = N_s I$, la ambiencia es lo que la
componente principal deja de la entrada:

$$
C_2 \;=\; N_s \cdot x[\,n_0 + i\,] \;-\; C_1
$$

con $N_s$ = `covsteps`, $n_0$ = `syn_offset` y $x$ el historial — que **ya está
en memoria**. Eso ahorra dos transformadas inversas, la mitad de la síntesis.

**10. La trama emitida** sale de `out_time[·][syn_offset + i]`, sin dividir por
`covsteps+1`: esa división la hace `emitBlock()`, igual que para el motor de
banda ancha.

---

## 6. Qué significa la salida

Veinte PCA, **una sola aplicación**. Esa asimetría es la que hay que entender
para leer lo que sale por C1 y por C2.

### 6.1 $G_1(f)$ no es un proyector

Cada $P_{1,b}$ sí lo es: rango 1, autovalores $\{1,0\}$, y proyectar dos veces
es proyectar una. Pero $G_1(f)$ es una **combinación convexa** de los veinte,
con pesos $W_b(f)\ge 0$ que suman uno. De ahí salen tres propiedades exactas:

- es simétrica y semidefinida positiva;
- su **traza vale exactamente 1** en todo bin, porque
  $\operatorname{tr} G_1 = \sum_b W_b \cdot \operatorname{tr} P_{1,b} = \sum_b W_b = 1$;
- sus autovalores son por tanto $\lambda$ y $1-\lambda$, con $\lambda\in[\tfrac12,1]$.

Y es un proyector **sólo cuando $\lambda = 1$**, lo que ocurre exclusivamente si
todos los $P_{1,b}$ que pesan en ese bin son el mismo — es decir, si las bandas
que solapan ahí **están de acuerdo sobre dónde está la fuente**.

### 6.2 La costura, y cuánto cuesta exactamente

Para la mezcla de dos proyectores cuyos ejes difieren $\Delta\theta$, a pesos
iguales:

$$
\lambda_{1,2} \;=\; \tfrac12\bigl(1 \pm \cos\Delta\theta\bigr),
\qquad
\lambda_{\min} \;=\; \sin^{2}\!\Bigl(\frac{\Delta\theta}{2}\Bigr)
$$

$\lambda_{\min}$ es la **fuga**: la fracción de energía que en ese bin no acaba
donde la banda dominante habría dicho.

| $\Delta\theta$ | 0° | 10° | 20° | 30° | 45° | 60° | 90° |
|---|---|---|---|---|---|---|---|
| $\lambda_{\min}$ | 0 | 0,0076 | 0,030 | 0,067 | 0,146 | 0,250 | 0,500 |
| en dB | −∞ | −21,2 | −15,2 | −11,7 | −8,3 | −6,0 | −3,0 |

Es la **cota superior**: el reparto 50/50 sólo se da justo en el punto medio
entre dos centros. Con el 85/15 típico de un bin cualquiera la fuga es menor —a
30° de desacuerdo, −14,8 dB en vez de −11,7.

### 6.3 Lo que pasa de verdad

Medido sobre *I Am In Love*, banco de producción, con el desacuerdo entre bandas
**adyacentes** que ya guarda cada `.npz`:

```
  desacuerdo entre bandas vecinas:   mediana  7,2°     p90  51,6°     p99  86,4°
  fuga en la costura (cota 50/50):   mediana -24,0 dB  p90  -7,2 dB   p99  -3,3 dB
```

La mediana dice que casi siempre las bandas vecinas coinciden dentro de siete
grados y la costura es limpísima. El p90 dice que **una de cada diez veces
discrepan más de cincuenta grados**, y ahí el reparto es casi mitad y mitad.

Y ahí está la clave de lectura: **ese desacuerdo es la información, no el
ruido.** Es exactamente lo que el motor de banda ancha no podía ver — el
contrabajo con su eje y los platos con el suyo. Cuando aparece, el algoritmo
hace un **traspaso suave** entre las dos direcciones en vez de un salto. Un salto
duro en el borde de banda sería una discontinuidad en frecuencia, y se oiría.

### 6.4 Lo que no se pierde nunca

$G_1 + G_2 = I$ es exacto, así que **C1 + C2 es la entrada**, pase lo que pase en
las costuras. La descomposición puede ser blanda; destructiva no es nunca.

Y el mismo suavizado ocurre en el tiempo: el anillo suma los proyectores de las
últimas $N_s$ ventanas, de modo que $G_1$ está suavizado **en frecuencia** (entre
bandas vecinas) y **en el tiempo** (entre ventanas sucesivas) a la vez.

### 6.5 Qué son entonces C1 y C2

- **C1** es, en cada frecuencia, la parte del campo estéreo alineada con la
  dirección dominante **de la banda a la que esa frecuencia pertenece**.
- **C2** es lo que esa dirección no explica: reverberación, fuentes secundarias
  descorrelacionadas, y la fuga de las costuras.

Frente al motor de banda ancha, que impone un solo eje a todo el espectro y por
tanto un solo compromiso, aquí cada banda se descompone con su propia dirección.

**Lo que eso debería oírse:** el C2 de este motor contiene *menos* de la fuente
dominante que el C2 de banda ancha, porque cada banda le quita su propia
dirección en lugar de una media global. Es la afirmación audible que hay que
comprobar — y es para lo que sirven los wav por banda.

### 6.6 La medida que falta

Cruzando `delta_erb` con el desacuerdo medido, mismo tema y misma ventana:

| `delta_erb` | bandas | desacuerdo medio | fuga por costura | costuras |
|---|---|---|---|---|
| 1 | 39 | 5,37° | −26,6 dB | 38 |
| 2 | 21 | 6,91° | −24,4 dB | 20 |
| 4 | 11 | 7,69° | −23,5 dB | 10 |

Más bandas hacen cada costura más limpia —los vecinos están más cerca y
coinciden más— pero hay más costuras. Menos bandas, al revés.

**Eso es una indicación, no una medida**, y conviene no tratarla como tal: las
costuras no llevan todas la misma energía, y la fuga sólo actúa en la zona de
solape, pesada por las propias máscaras. La medida honesta es

$$
\text{fuga} \;=\; \frac{\sum_k \lambda_{\min}\bigl(G_1(f_k)\bigr)\,|X(f_k)|^2}
                       {\sum_k |X(f_k)|^2}
$$

— el autovalor menor de $G_1$ en cada bin, pesado por la energía que hay
realmente ahí. Sale directo de lo que el `.npz` ya guarda, `theta` y las
máscaras, y es lo primero que debería calcular el script de análisis: contestaría
de verdad por qué `delta_erb 2` y no 1 ni 4.

---

### 6.7 El caso que dio origen a todo esto

*Shelly Manne and His Men, «I Am In Love».* Una trompeta muy paneada a la
izquierda y una batería muy paneada a la derecha. Esta grabación fue durante
mucho tiempo **la referencia para explicar por qué el PCA se atraganta con
material comercial**, y es la que dio pie al modo beta.

Lo que se oye con el motor de banda ancha: cuando entra un golpe de batería, la
trompeta —que es sonido directo y debería ir a C1— **se cae al canal de
ambiente**. No es un fallo de la implementación. Es lo que un solo eje tiene que
hacer cuando hay dos fuentes fuertes en direcciones opuestas.

**Medido.** Los grupos salen solos de los datos, sin decírselo:

```
  bandas con el eje a la IZQUIERDA (>+20°): 15, de  395 a  3067 Hz   <- la trompeta
  bandas con el eje a la DERECHA  (<-20°): 19, de   20 a 18207 Hz   <- bombo abajo, platos arriba
```

Y el eje de banda ancha, sacado del mismo script con el suelo tan alto que sólo
quede una banda:

| | % del tiempo | eje de banda ancha |
|---|---|---|
| manda la izquierda (>6 dB) | 8,6 % | **+42,3°** |
| parejos | 56,1 % | +18,2° |
| manda la derecha (>6 dB) | 35,3 % | **−37,8°** |

**Correlación entre «quién domina en ese instante» y el eje de banda ancha:
−0,814.** El eje no está describiendo la escena: está describiendo **cuál de los
dos instrumentos está pegando**. Su recorrido entre una situación y otra es de
ochenta grados.

Y ahí está el mecanismo completo. Con el golpe, el eje principal se va a −38°, la
dirección de la batería. La trompeta está en +39°, o sea a **77 grados del eje
principal** — casi ortogonal. Y todo lo ortogonal a C1 es, por definición, C2.

### 6.8 Dos arreglos distintos para el mismo problema

**El modo beta**, que nació de aquí, ataca la **amplitud** de la pelea. Escala el
lado según lo correlacionados que estén los canales:

$$w = 0{,}55 + 0{,}45\,\bigl|\rho_{LR}\bigr|$$

Con material muy ancho $\rho_{LR}$ baja, $w$ cae hacia 0,55, y como
$\tan\theta = w\,(L-R)/(L+R)$ **todo el recorrido de panorama se comprime de
±45° a ±28,8°**. La pelea sigue existiendo; se pelea en un espacio más pequeño.

**NAE_ERB** ataca la **causa**. Las bandas de la trompeta sólo miran su propio
trozo de espectro, donde la batería apenas tiene energía, así que su eje se queda
en +39° pase lo que pase abajo. La correlación con «quién domina» baja de −0,814
a −0,616 incluso dentro de las bandas de la trompeta, y eso es todo lo que queda
del arrastre.

**No son alternativas, son complementarios**, y conviene tenerlo claro porque se
parecen en el efecto audible:

| | qué hace | qué no hace |
|---|---|---|
| beta | encoge la excursión del eje | el eje sigue siendo uno solo para todo el espectro |
| ERB | da un eje a cada banda | no toca la excursión dentro de una banda |

Y las dos direcciones de mejora dentro de ERB atacan mitades distintas de lo
mismo: **más bandas** separan mejor la trompeta de la batería, y **ventana más
larga** suaviza lo que quede de pelea. Por eso una configuración con
`delta_erb 1` y `band_min_hz 62.5` —cuarenta bandas y 128 ms— rinde aquí mucho
más que en material donde no hay dos fuentes disputándose el eje.

> Si hace falta una sola frase para justificar el motor por bandas, es esta
> grabación. El resto de las medidas dicen que NAE_ERB **representa** cosas que
> el banda ancha no puede; esta dice que **arregla** algo que se oía mal.

---

## 7. Los dos casos especiales

Ninguno de los dos existe en el script de Python. Los dos salieron de un
problema real en producción, no de teoría.

### 7.1 El camino rápido mono

Con una fuente **mono exacta** —`L == R` bit a bit— la anchura mezcla `pa·x+pb·x`
contra `pb·x+pa·x`, que son iguales porque la suma IEEE es conmutativa, y `side`
es **exactamente cero**. Entonces la covarianza de toda banda es $\begin{pmatrix}S_{mm}&0\\0&0\end{pmatrix}$
y el proyector principal es exactamente $\begin{pmatrix}1&0\\0&0\end{pmatrix}$.

Eso no se aproxima: **se resuelve**. Se escribe esa constante en el anillo y se
saltan los bucles de banda y el problema propio.

Y hay **dos profundidades**, que no son el mismo número:

- Una **ranura** del anillo es el proyector de una ventana entera, así que es la
  mono sólo cuando las últimas `n_cov/256 = 12` tramas han sido mono todas. Una
  trama mono entrando en una ventana que todavía guarda el side de lo anterior
  **no** es una ventana mono — contarlo por tramas costó 0,038 de fondo de
  escala en la transición.
- El camino **directo**, que se salta hasta las transformadas, necesita que las
  `covsteps` ranuras sean esa constante, o sea `12 + 3 − 1 = 14` tramas mono
  seguidas. Ahí $G_1 = N_s \begin{pmatrix}1&0\\0&0\end{pmatrix}$, y multiplicar un espectro por una
  constante no es una convolución: C1 son tres copias del historial y C2 es cero
  exacto, sin una sola transformada.

No hay estado que guardar y restaurar en los saltos mono↔estéreo: **el anillo es
el estado**, y lo que el camino rápido escribe en él es exactamente lo que el
lento habría escrito.

No tiene configuración porque no es una elección entre dos comportamientos: la
salida es la misma a nivel de bit.

### 7.2 El gate de residuo

Aquí está la diferencia numérica que costó un día entero de diagnóstico.

Con mono exacto C2 es **matemáticamente cero**. El motor de banda ancha lo dice
exactamente, porque **proyecta** sobre el segundo eje y una proyección de cero es
cero. Este motor **resta**, y la resta es una identidad exacta en álgebra pero no
en coma flotante: donde el resultado verdadero es cero, lo que queda es el
redondeo de los dos operandos — unos `1e-15` de la entrada, del orden de
**−400 dBFS**.

Eso no es audio por ninguna medida: 24 bits llegan a −144 dB. Pero no es cero, y
aguas abajo es carísimo. Medido: alimentando un convolucionador particionado,
convirtió el hilo de la partición más profunda —el que lleva la cola de la
respuesta impulsional— de **1,4% de un núcleo a 19,6% con picos por encima del
65%**, lo bastante tarde como para tumbar el driver FireWire.

**Cero exacto no cuesta nada ahí; `1e-20` lo cuesta todo.** El gate no es
pulcritud, es el arreglo:

$$
\gamma \;=\; 10^{-10} \cdot N_s \cdot \max_{\text{ventana de análisis}} |x|
$$

$$
\max|C_1| < \gamma \;\Rightarrow\; C_1 := 0
\qquad
\max|C_2| < \gamma \;\Rightarrow\; C_2 := 0
$$

Dos detalles que importan:

- **Relativo, no absoluto**: el residuo escala con la entrada, y un suelo
  absoluto ajustado en un pasaje suave dejaría pasar uno fuerte. La razón `1e-10`
  está cinco órdenes por encima de donde vive el redondeo y cinco por debajo de
  cualquier ambiencia de una grabación real.
- **Medido sobre la ventana de análisis, no sobre la trama emitida**: las dos
  componentes salen de filtrar la ventana entera, así que es con eso con lo que
  escala su redondeo. Y una trama de silencio entre dos fuertes tiene pico cero
  mientras todavía arrastra la cola de lo anterior — medirla sola dejaba el gate
  cerrado justo en la transición para la que existe.
- **Por bloque y no por muestra**, para que una componente se emita o no se
  emita, y nunca se le recorten sus propias muestras pequeñas por debajo.

---

## 8. Diferencias con el script de Python

El script no es una maqueta del motor: es un **banco de estudio** que implementa
tres formulaciones distintas para poder compararlas, y el motor implementa sólo
una de ellas.

| | C++ `nae_erb` | Python `nae_natambio_erb.py` |
|---|---|---|
| Formulaciones | sólo la equivalente a `--filtering matrix` | `whole`, `block`, `matrix` |
| **C2** | **por resta**, `covsteps·x − C1` | **por proyección**, con su propio anillo `P2` |
| Inversas por paso | **2** | **4** |
| Fase del banco | **cero** siempre: multiplicador real, sin kernels (3.4) | lineal con $T_0=\texttt{nfft}/2$ en `whole`/`block`; cero en `matrix` |
| Ventana de síntesis | **siempre `n_cov`** (3072 aquí) | separable; **por defecto `2·n_pca`** (1536 aquí) |
| Suelo de banda | `<band_min_hz>` en **Hz** | `alpha × fs/n_cov`, **relativo** |
| Ventana de análisis | **derivada del suelo** si falta la etiqueta (9.1) | siempre explícita, atada a `alpha` |
| Camino rápido mono | sí | no |
| Gate de residuo | sí | no |
| Banda baja ancha | no | `--low-band-mode` |
| Eje forzado (control) | no | `--same-axis` |
| Control de aliasing | no | sí, contra una ventana 4× |
| Solver 2×2 | `eigen_2x2_symmetric()` propio | `np.linalg.eig` + ordenación explícita |

Y lo que **sí** es idéntico, deliberadamente: el plegado `fold` (0 en DC, 2 en
medio, 1 en Nyquist), la normalización `1/(N(N−1))`, la forma y el orden de los
gammatone, la normalización punto a punto de las máscaras, el anillo de
`covsteps` proyectores y la división final por `covsteps+1`.

### 8.1 Las tres formulaciones del script

- **`whole`** — la literal: filtra la señal en 20 bandas con FIR de fase lineal,
  corre un PCA por banda, suma. Lenta y con el retardo común de los filtros, pero
  es la referencia contra la que se comprueba que las otras dos dicen lo mismo.
- **`block`** — un par de transformadas por ventana, todas las bandas desde ahí,
  los bucles invertidos. Realizable, pero el contenido por banda se diferencia
  del exacto por el envolvimiento circular del bloque.
- **`matrix`** — la del motor: el banco no existe en el camino de audio.

### 8.2 El suelo relativo contra el absoluto

El script parametriza el suelo como `b_min = alpha · fs/n_cov`; el motor lo toma
en Hz. **No es cosmético.** Para comparar honestamente una ventana de 16 ms
contra una de 64 hay que mantener el suelo fijo, y con la forma relativa eso
obliga a cambiar `alpha` de 2 a 8 al mismo tiempo. Los dos ficheros que sostienen
la cifra de «9,65° a 3,65°» son precisamente `a2` y `a8` con `b_min = 125,00 Hz`
en los dos. En Hz esa conversión mental desaparece: el suelo es una anchura
física y no debe moverse cuando se mueve la ventana.

### 8.3 La resta contra la proyección

El script proyecta C2 con su propio anillo y dos inversas más. El motor resta.
Producen el mismo número salvo redondeo **cuando C2 es una señal real**; se
separan exactamente donde C2 verdadero es cero, que es el caso mono. Por eso el
gate existe en el motor y no en el script — y por eso el script **no reproduce**
el problema que el gate arregla.

---

## 9. Qué decide cada parámetro

| tag | efecto | coste |
|---|---|---|
| `<steps_length>` | la ventana de reconstrucción, **y la latencia**: `covsteps × 256` | 3 → 16,0 ms |
| `<band_min_hz>` | resolución en frecuencia: suelo de anchura y de separación, **y de ahí la ventana de análisis** | pocas bandas arriba o abajo, y la CPU que la ventana traiga |
| `<delta_erb>` | separación de centros → número de bandas | barato: no cambia el número de transformadas |
| `<cov_window_ms>` | opcional: fuerza la ventana de análisis | CPU, se dobla al doblar la ventana; **cero latencia** |

Y lo que **no** es configurable, a propósito: la forma de los filtros, el orden,
el rango cubierto y la regla de separación. Se barrieron fuera de línea y
ninguno se ganó un mando.

### 9.1 La ventana sale del suelo

`<cov_window_ms>` no hace falta darla. Si el fichero calla, el motor la deriva:

$$
T_{\text{cov}} \;=\; \frac{N_{\text{bins}}}{B_{\min}}
\qquad\Longrightarrow\qquad
\texttt{cov\_window\_ms} = \frac{8000}{\texttt{band\_min\_hz}}
$$

con $N_{\text{bins}} = 8$, y **el resultado no depende de la frecuencia de
muestreo**: es una duración, no un número de muestras.

Lo que se está fijando es el **producto tiempo-ancho de banda de la banda más
estrecha**, $B_{\min}\cdot T$: el número de estimaciones espectrales
independientes con que se construye su covarianza. Ocho.

| `band_min_hz` | ventana derivada | `n_cov` a 48 kHz | periodos | coste/bloque |
|---|---|---|---|---|
| 500 Hz | 16,0 ms | 768 | 3 | 154 µs |
| 250 Hz | 32,0 ms | 1536 | 6 | 259 µs |
| **125 Hz** | **64,0 ms** | **3072** | **12** | **523 µs** |
| 100 Hz | 80,0 ms | 3840 | 15 | 659 µs |
| 62,5 Hz | 128,0 ms | 6144 | 24 | 1072 µs |

**El ocho no es arbitrario, y conviene que quede dicho.** Con $B_{\min}\cdot T = 8$
el error relativo de una estimación de potencia va como $1/\sqrt{8}\approx 35\,\%$,
que suena flojo; pero el movimiento del eje medido justo en ese punto es 3,65°
de mediana, contra los 3,34° del motor de banda ancha. Empíricamente basta. Por
debajo no: 125 Hz sobre una ventana de 16 ms son **dos bins**, y dos bins no son
una estimación.

También es una coincidencia que vale la pena registrar: 125 Hz y 64 ms se
eligieron por caminos separados, y caen sobre la regla con cuatro decimales
exactos —$48000/3072 = 15{,}6250$ Hz por bin, $125/15{,}6250 = 8{,}0000$.

### 9.2 Cota inferior, no igualdad

La etiqueta sigue existiendo y se respeta si está. Lo que hace la derivación es
que **el valor por defecto sea siempre coherente** y que las dos etiquetas no
puedan ponerse en contra por descuido.

- Sin `<cov_window_ms>`: se deriva, y el arranque lo dice.
- Con ella **por encima** del suelo: se usa tal cual, sin comentarios. Alargar la
  ventana a suelo fijo sube el producto por encima de ocho y estima mejor; es
  algo legítimo de querer y no se estorba.
- Con ella **por debajo**: se usa igual —el banco sigue reconstruyendo y el motor
  sigue corriendo, así que no es un error— pero avisa con la cifra exacta y el
  remedio:

```
NAE_ERB: t: 2.00 bins across the narrowest band (125.00 Hz at 62.50 Hz per bin)
NAE_ERB: t: WARNING: <cov_window_ms> of 16 ms leaves only 2 bins across a
125 Hz band, under the 8 this engine is built on. Drop the tag to derive it
(64 ms) or raise <band_min_hz>.
```

El número de bins por banda del suelo se reporta **siempre**, se haya derivado o
no, y se calcula después del redondeo a periodos enteros: es la cifra con la que
el motor va a correr de verdad, no la que se pidió.

> **El script de Python no hace esto.** Ahí la ventana y el suelo siguen atados
> por `alpha` (sección 8.2), que es la parametrización inversa: el suelo sale de
> la ventana en vez de al revés. Una tirada del script sigue necesitando que
> `alpha` y la ventana se muevan juntas a mano.

---

## 10. Lo que está comprobado y cómo

- **El banco reconstruye.** $\max_f \bigl|\sum_b W_b(f) - 1\bigr|$ se verifica antes de correr
  ningún PCA; el script se niega a continuar si falla.
- **Las bandas suman la entrada** (control en el tiempo, tras quitar el retardo
  común).
- **Con un solo eje para todas las bandas** (`--same-axis`) la cadena entera es
  lineal en mid/side y el banco suma la entrada, así que la suma de las bandas
  tiene que ser **exactamente** la tirada de banda ancha. Es el control
  estructural.
- **El aliasing de síntesis** se mide contra la misma síntesis por una
  transformada 4× más larga, componente a componente. Es el control que faltaba
  cuando el envolvimiento se estaba oyendo y la suma no lo veía.
- **El motor de banda ancha quedó bit a bit intacto** tras todos los cambios de
  esta rama: alpha, beta, y alpha con `pan_scale 0.5`, diferencia máxima cero.

---

## 11. Lo que todavía no está escrito

- Qué se oye exactamente en cada banda. Es lo que va a contestar el próximo
  trabajo: wav por banda y el script de análisis.
- Por qué `delta_erb 2` y no 1 o 4. La sección 6.6 da la primera cifra y deja
  planteada la medida honesta —la fuga pesada por energía—, pero no está
  calculada. Los 36 `.npz` de `~/pca_stereo/samples` tienen la respuesta dentro.
- La justificación perceptual del suelo de 125 Hz frente a 62,5.
