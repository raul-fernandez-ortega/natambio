# Parametrización del operador $G$ en NatAmbio XTC

**Autor:** Raúl Fernández Ortega  
**Fecha:** agosto de 2026

> **Resumen —** *El algoritmo XTC de NatAmbio, descrito en [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md), reduce todo el problema de la diafonía acústica a un único operador, la función G, que es la relación entre el camino cruzado y el camino directo. Esta nota aborda cómo se determina ese operador en la práctica. Se parte de la vía teóricamente exacta —emplear respuestas HRTF individuales medidas— y se descarta por inviable fuera del ámbito científico, para pasar al análisis de cinco bases de datos HRTF públicas (HUTUBS, RIEC, BiLi, CIPIC y ARI). Ese análisis muestra que el ITD y el valor de banda ancha del ILD presentan una dispersión interindividual reducida en los azimuts propios de la reproducción estéreo, y admiten por tanto un modelo de regresión de baja dimensionalidad; en cambio, la forma espectral del ILD es marcadamente irregular y específica de cada anatomía, por lo que se sustituye por un ajuste empírico monótono gobernado por un único parámetro α. El operador resultante queda descrito por cuatro parámetros ajustables —ITD, ILD promedio, azimut Θ y α— y se realiza en fase mínima por parsimonia. La nota discute además dos cuestiones prácticas: por qué el ILD óptimo por escucha resulta sistemáticamente superior al derivado de las HRTF, con las reflexiones tempranas como explicación propuesta, y por qué NatAmbio no necesita las geometrías de azimut mínimo propias de RACE. Cierra con un procedimiento de ajuste paso a paso para los cuatro parámetros.*

## Abreviaturas

| Sigla | Significado |
|---|---|
| ARI | *Acoustics Research Institute* (Viena); base de datos HRTF |
| BiLi | *Base de données de l'Écoute Binaurale* (IRCAM); base de datos HRTF |
| CIPIC | *Center for Image Processing and Integrated Computing* (UC Davis); base de datos HRTF |
| DRC | *Digital Room Correction*: corrección digital de sala |
| DTF | *Directional Transfer Function*: HRTF sin su componente común a todas las direcciones |
| ERB | *Equivalent Rectangular Bandwidth*: ancho de banda rectangular equivalente |
| FIR | *Finite Impulse Response*: respuesta impulsional finita |
| HRTF | *Head-Related Transfer Function*: función de transferencia relativa a la cabeza |
| HUTUBS | Base de datos HRTF de la Technische Universität Berlin |
| IIR | *Infinite Impulse Response*: respuesta impulsional infinita |
| ILD | *Interaural Level Difference*: diferencia interaural de nivel |
| ITD | *Interaural Time Difference*: diferencia interaural de tiempo |
| RACE | *Recursive Ambiophonic Crosstalk Elimination* |
| RIEC | *Research Institute of Electrical Communication* (Tohoku University); base de datos HRTF |
| SOFA | *Spatially Oriented Format for Acoustics*: formato de intercambio de medidas HRTF |
| XTC | *Crosstalk Cancellation*: cancelación de diafonía |

## Notación

| Símbolo | Significado |
|---|---|
| $H_{direct},\ H_{cross}$ | Caminos acústicos directo y cruzado (versiones simétricas) |
| $G = H_{cross}/H_{direct}$ | Función de transferencia cruzada normalizada |
| $G_l,\ G_r$ | Versiones izquierda y derecha de $G$ en el modelo asimétrico |
| $F^{direct},\ F^{cross}$ | Filtros FIR resultantes (camino directo y cruzado) |
| $\delta$ | Impulso unitario (elemento neutro de la convolución) |
| $\ast$ | Operador convolución |
| $N$ | Número de términos (iteraciones) del sumatorio |
| $f$ | Frecuencia |
| $\Theta$ | Azimut de incidencia (semiángulo entre altavoces; separación total $2\Theta$) |
| $\text{ITD}$ | Diferencia interaural de tiempo, en µs |
| $`\text{ILD}_{avg}`$ | Valor de banda ancha del ILD, en dB (denominado $`\text{ILD}_{dB}`$ en la nota principal) |
| $`a = 10^{-\text{ILD}_{avg}/20}`$ | Factor de atenuación lineal asociado al ILD |
| $`\text{ILD}_{spectrum}(\Theta, f)`$ | Forma espectral del ILD, normalizada para no aportar nivel |
| $\alpha$ | Parámetro de pendiente del modelo espectral de ILD |

## El operador $G$

Como ya se ha descrito en [Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio](xtc_filters_es.md) y en [Aplicación de NatAmbio XTC en entornos no simétricos](xtc_no_simetrico_es.md), el modelo XTC depende de un único operador que condensa múltiples aspectos acústicos y de fisiología de la escucha:

```math
G = \frac{H_{cross}}{H_{direct}}
```

$G$ representa el operador acústico normalizado entre el camino cruzado y el camino directo. En el dominio frecuencial puede interpretarse como el cociente complejo

```math
G(f) = \frac{H_{cross}(f)}{H_{direct}(f)}
```

mientras que en el dominio temporal corresponde a su respuesta impulsional equivalente, obtenida mediante la convolución de $H_{cross}$ con el filtro inverso de $H_{direct}$.

Como ya se ha comentado, es lógico pensar que la sombra de la cabeza y el torso hacen razonable asumir que $|G| < 1$, dado que el camino directo desde el altavoz está despejado y el camino cruzado está apantallado.

En el modelo desarrollado en NatAmbio se han separado dos componentes de los caminos acústicos: el entorno acústico (conjunto altavoces + sala), cuya respuesta debe corregirse mediante técnicas específicas ajenas al algoritmo XTC (DRC, ecualización paramétrica, etc.), y el camino acústico comprendido entre el altavoz ya ecualizado y los oídos del oyente. Es sobre este segundo componente donde actúa $G$.

## Relación entre $G$ y las HRTF

Las definiciones anteriores de $H_{direct}$ y $H_{cross}$ coinciden con los caminos acústicos del modelo [HRTF](https://en.wikipedia.org/wiki/Head-related_transfer_function). En consecuencia, una primera forma de construir el operador $G$ consiste en partir directamente de respuestas impulsionales HRTF medidas para el oyente y el azimut considerados. Por ejemplo, en esta medida de la base de datos ARI, que incluye más de 100 sujetos individuales:

![Caminos directo y cruzado de una HRTF individual a 30° de azimut](images/hrtf_diff_az0030.0.png)

Si se dispone de estas impulsivas, obtener $G$ consiste en calcular el operador inverso de $H_{direct}$ y convolucionarlo con $H_{cross}$. El resultado es un operador $G$ que puede emplearse como entrada del algoritmo recursivo NatAmbio XTC para generar los filtros canceladores:

```math
F^{cross} = \sum_{i=1}^{N} - G^{2i-1}
```

```math
F^{direct} = \delta + \sum_{i=1}^{N} G^{2i}
```

Estas expresiones son el resultado exacto del álgebra de las cancelaciones sucesivas; el filtro efectivamente realizado aplica la forma espectral una sola vez por escalón, según se detalla en [Aplicación del espectro ILD en la serie](xtc_filters_es.md#aplicaci%C3%B3n-del-espectro-ild-en-la-serie).

Si se desea afinar el cálculo, dado que realmente nadie es anatómicamente simétrico por completo, se aplicaría el [modelo asimétrico](xtc_no_simetrico_es.md), donde $G_l$ y $G_r$ serían distintas:

![Caminos directo y cruzado del lado contrario, a −30° de azimut](images/hrtf_diff_az-030.0.png)

Hasta aquí el procedimiento parece completamente definido. Sin embargo, antes de adoptar este operador como modelo definitivo de $G$, conviene analizar qué información contienen realmente las respuestas HRTF y cuál de ella resulta útil para la cancelación de diafonía en un entorno doméstico.

### Síntesis de $G$ a partir de bases de datos públicas

El proceso de medición para obtener las HRTF personales, y así generar el filtro XTC, es una tarea especialmente compleja, inviable fuera del ámbito científico y tecnológico, lo cual dificulta enormemente el uso de un modelo HRTF individual como método estándar doméstico para determinar $G$.

Existe otra opción, y es la de emplear la información de las bases de datos HRTF públicas. Existen numerosas bases de datos de este tipo, donde se recogen medidas de diversos individuos, dado que la HRTF es una característica individual de cada uno. Puede haber semejanzas, pero no existen dos HRTF idénticas, ni siquiera para los dos oídos de la misma persona. En este repositorio se pueden encontrar muchas de ellas:

[https://sofacoustics.org/data/database/](https://sofacoustics.org/data/database/)

Cuando se explora una base de datos HRTF lo primero que se comprueba es su variedad:

![Dispersión entre sujetos de una base de datos HRTF a 30° de azimut (1)](images/hrtf_diff_az0030.0_2.png)

![Dispersión entre sujetos de una base de datos HRTF a 30° de azimut (2)](images/hrtf_diff_az0030.0_3.png)

![Dispersión entre sujetos de una base de datos HRTF a 30° de azimut (3)](images/hrtf_diff_az0030.0_4.png)

Una primera aproximación consiste en promediar directamente todas las respuestas impulsionales contenidas en una base de datos HRTF, buscando las características comunes a diferentes individuos. Sin embargo, antes de realizar dicho promedio resulta conveniente preguntarse qué propiedades de una HRTF son realmente relevantes para construir el operador $G$.

### Promediado de características HRTF

La propuesta de modelado de $G$ en NatAmbio incluye una descomposición en tres componentes que representan tres aspectos característicos de dichas medidas:

```math
G \approx \delta \left( \text{ITD}(\Theta),\ \text{ILD}_{avg}(\Theta) \right) \ast \text{ILD}_{spectrum}(\Theta, f)
```

La motivación detrás de esta descomposición es la siguiente:

- El **ITD** representa la diferencia temporal en la llegada del sonido al oído ipsilateral y al oído contralateral. En el caso de NatAmbio se asume un parámetro constante con la frecuencia, aunque en la realidad sí presenta variación con ella.

- El **ILD** representa la diferencia de niveles entre la señal percibida por el oído ipsilateral y la percibida por el contralateral. También es dependiente de la frecuencia, como en el caso del ITD. En el modelado de NatAmbio, el ILD se descompone en un valor promedio de banda ancha, $`\text{ILD}_{avg}`$, y una curva espectral, $`\text{ILD}_{spectrum}`$, asumiendo que la dependencia del ILD con la frecuencia es más significativa que la del ITD.

Un análisis de algunas de las bases de datos HRTF más populares refleja que ni el ITD ni el $`\text{ILD}_{avg}`$ presentan variaciones muy grandes de individuo a individuo[^1].

**BiLi** — [https://sofacoustics.org/data/database/bili%20(dtf)](https://sofacoustics.org/data/database/bili%20(dtf))

![ITD frente a azimut, base de datos BiLi](images/bili_ITD_vs_azimuth_all.png)

![ILD de banda ancha frente a azimut, base de datos BiLi](images/bili_ILD_broadband_vs_azimuth_all.png)

**ARI** — [http://sofacoustics.org/data/database/ari](http://sofacoustics.org/data/database/ari)

![ITD frente a azimut, base de datos ARI](images/ari_ITD_vs_azimuth_all.png)

![ILD de banda ancha frente a azimut, base de datos ARI](images/ari_ILD_broadband_vs_azimuth_all.png)

**CIPIC** — [http://sofacoustics.org/data/database/cipic](http://sofacoustics.org/data/database/cipic)

![ITD frente a azimut, base de datos CIPIC](images/cipic_ITD_vs_azimuth_all.png)

![ILD de banda ancha frente a azimut, base de datos CIPIC](images/cipic_ILD_broadband_vs_azimuth_all.png)

**HUTUBS** — [http://sofacoustics.org/data/database/hutubs/](http://sofacoustics.org/data/database/hutubs/)

![ITD frente a azimut, base de datos HUTUBS](images/HUTUBS_ITD_vs_azimuth_all.png)

![ILD de banda ancha frente a azimut, base de datos HUTUBS](images/HUTUBS_ILD_broadband_vs_azimuth_all.png)

**RIEC** — [http://sofacoustics.org/data/database/riec](http://sofacoustics.org/data/database/riec)

![ITD frente a azimut, base de datos RIEC](images/RIEC_ITD_vs_azimuth_all.png)

![ILD de banda ancha frente a azimut, base de datos RIEC](images/RIEC_ILD_broadband_vs_azimuth_all.png)

La información básica de todas estas bases de datos HRTF está recogida en [https://www.sofaconventions.org/mediawiki/index.php/Files](https://www.sofaconventions.org/mediawiki/index.php/Files).

Las barras de error representan la estimación de la desviación estándar asociada a cada promediado. Se puede apreciar que muestran dispersiones bastante limitadas.

Es sencillo obtener una primera estimación de $\text{ITD}$ y $`\text{ILD}_{avg}`$ por regresión:

![ITD promedio frente a azimut para las cinco bases de datos](images/ITD_vs_azimuth.png)

![ILD de banda ancha promedio frente a azimut para las cinco bases de datos](images/ILD_vs_azimuth.png)

Se aprecia una diferencia notable entre los valores de $`\text{ILD}_{avg}`$ dados por RIEC y los del resto de bases de datos analizadas. Esta discrepancia merecería un estudio de sus motivos, pero en esta nota no hay intención de analizarla en detalle.

Las ecuaciones propuestas en el modelo de regresión de NatAmbio XTC son:

```math
\text{ITD} = 5.6746 \cdot \Theta + 184.1315 \cdot \sin \Theta
```

```math
\text{ILD}_{avg} = -0.10 + 0.407 \cdot \Theta - 0.0025 \cdot \Theta^{2}
```

con $\Theta$ en grados en ambos casos, $\text{ITD}$ en µs e $`\text{ILD}_{avg}`$ en dB.

En cuanto a la forma espectral del ILD, los modelos presentan curvas irregulares, con picos y valles que, muy probablemente, se deban a factores anatómicos:

![ILD normalizado frente a frecuencia, azimut 10°](images/ILD_normalized_az10.png)

![ILD normalizado frente a frecuencia, azimut 20°](images/ILD_normalized_az20.png)

![ILD normalizado frente a frecuencia, azimut 30°](images/ILD_normalized_az30.png)

![ILD normalizado frente a frecuencia, azimut 60°](images/ILD_normalized_az60.png)

![ILD normalizado frente a frecuencia, azimut 90°](images/ILD_normalized_az90.png)

El riesgo de incluir estos picos y valles en la modelización de $`\text{ILD}_{spectrum}(\Theta, f)`$ para NatAmbio es que se produzcan brillos o pitidos durante la escucha, dado que puede ocurrir que no tengan correspondencia con los propios del sujeto oyente en cada caso.

Por este motivo se ha procedido a realizar un ajuste empírico sencillo, monótono y gobernado por dos parámetros:

```math
\text{ILD}_{spectrum}(\Theta, f) = \alpha \cdot 10 \cdot \log_{10}(f/1000 + 1) \cdot \sin(\Theta)
```

Expresado en dB, el ajuste crece monótonamente con la frecuencia; es decir, la magnitud del camino cruzado decae de forma monótona, sin picos ni valles. El azimut $\Theta$ gobierna la magnitud global de la inclinación (a través de $\sin\Theta$) y el parámetro $\alpha$ gobierna su pendiente. Del estudio de los promedios de las diferentes bases de datos HRTF públicas resulta que, para $\Theta$ entre 10° y 30°, el valor adecuado es $\alpha \approx 1.5$ a $2.0$.

Todos los resultados obtenidos a partir del estudio de las bases de datos HRTF disponibles públicamente muestran que el $\text{ITD}$ y el valor de banda ancha $`\text{ILD}_{avg}`$ presentan una dispersión relativamente reducida entre individuos, especialmente para los azimuts pequeños habituales en reproducción estéreo. Sin embargo, esta estabilidad no implica que la distribución espectral del ILD sea igualmente estable. Al representar $`\text{ILD}_{spectrum}(f)`$ aparece una estructura mucho más irregular, con máximos y mínimos locales cuya posición y amplitud varían entre bases de datos e individuos. De ahí que la descomposición de $G$ propuesta para NatAmbio XTC sea:

```math
G \approx \delta \left( \text{ITD}(\Theta),\ \text{ILD}_{avg}(\Theta) \right) \ast \text{ILD}_{spectrum}(\Theta, f)
```

Es decir: $\text{ITD}$ e $`\text{ILD}_{avg}`$ admiten un modelo de baja dimensionalidad, mientras que $`\text{ILD}_{spectrum}`$ necesita ser regularizada, porque reproducir sus detalles individuales sin conocer al individuo introduce una precisión falsa.

## Implementación del modelo de $G$ en NatAmbio XTC

Finalmente, en NatAmbio se ha optado por diseñar los filtros XTC a partir de un modelado de $G$ en tres componentes: $\text{ITD}$, $`\text{ILD}_{avg}`$ e $`\text{ILD}_{spectrum}`$.

$\text{ITD}$ e $`\text{ILD}_{avg}`$ son configurables por el usuario, que puede emplear las aproximaciones por regresión para obtener unos valores iniciales en función del azimut $\Theta$ de su sistema de audio, y que además puede modificar esos valores en función de los resultados de escucha que obtenga, por ejemplo con [las señales de prueba generadas por los scripts `testing_XTC` de NatAmbio](../../tools/testing_XTC/README.md).

En cuanto a $`\text{ILD}_{spectrum}`$, NatAmbio integra la fórmula empírica propuesta, y tanto $\Theta$ como $\alpha$ son ajustables por el usuario.

Esto permite generar XTC en NatAmbio mediante un modelo simple, con cuatro parámetros —ITD, ILD promedio, azimut Θ y α— y una propuesta de valores iniciales, con el suficiente margen de libertad para efectuar el ajuste fino.

De esta manera, en NatAmbio XTC se dispone de un modelo de ajuste que puede operar con un sistema de sonido en el que los altavoces estén separados entre sí hasta 60° (es decir, azimut $\Theta = 30^{\circ}$). A ángulos mayores aumenta la variabilidad interindividual de $\text{ITD}$ e $`\text{ILD}_{avg}`$, mientras que la aproximación de $`\text{ILD}_{spectrum}`$ presenta una pendiente cada vez más pronunciada. A día de hoy no se cuenta con experiencia a separaciones entre altavoces mayores de 60°.

Por último, NatAmbio implementa la componente espectral del modelo de $G$ mediante filtros de fase mínima. La motivación no es reproducir la fase completa de una HRTF medida, sino mantener una representación temporal lo más compacta posible de la magnitud $`\text{ILD}_{spectrum}(f)`$, mientras que el retardo físicamente relevante se incorpora de forma explícita mediante el $\text{ITD}$.

De este modo, la respuesta asociada a $G$ concentra su energía a partir del instante definido por el $\text{ITD}$, sin requerir una respuesta simétrica alrededor de dicho instante ni introducir un retardo adicional para hacer causal el filtro. Esta propiedad resulta especialmente conveniente en una estructura recursiva, ya que las potencias sucesivas $G^{2}, G^{3}, \dots$ conservan una interpretación temporal clara: cada término de orden superior aparece únicamente después del retardo acumulado correspondiente.

La alternativa de utilizar directamente la fase compleja de respuestas HRTF medidas introduciría en $G$ una estructura temporal mucho más dependiente del individuo, de la geometría de medida y de las condiciones concretas de adquisición. NatAmbio evita incorporar esa complejidad cuando no existe una necesidad explícita de modelarla.

En este sentido, la elección de fase mínima debe entenderse como una decisión de diseño basada en parsimonia: introduce la mínima estructura temporal compatible con la magnitud espectral elegida, mientras que el retardo físicamente relevante se incorpora explícitamente mediante el $\text{ITD}$. El objetivo no es afirmar que la fase mínima constituya una solución perceptualmente óptima en términos generales, sino obtener una realización causal, compacta y coherente con el modelo físico adoptado por NatAmbio XTC.

## ¿Es necesario juntar los altavoces para NatAmbio XTC?

De todas las implementaciones XTC desarrolladas, quizás la más popular haya sido [RACE (*Recursive Ambiophonic Crosstalk Elimination*)](https://filmaker.com/papers/RGRM-RACE_rev.pdf) de Ambiophonics. RACE era un filtro IIR modelado a partir de cancelaciones recursivas, retardadas según el ITD y atenuadas según el ILD. Fue concebido para configuraciones Ambiodipole, con los altavoces situados a ángulos pequeños respecto al oyente.

Del análisis del algoritmo recursivo de RACE se comprueba que no incluye modelización de $`\text{ILD}_{spectrum}(f)`$. Una consecuencia razonable de esta simplificación es que el modelo debe funcionar mejor cuanto más próxima sea la respuesta acústica real a un único retardo y una única atenuación, lo que favorece geometrías de azimut pequeño. Un análisis del modelo de $`\text{ILD}_{spectrum}`$ a 5° de azimut permite comprobar por qué esta aproximación resulta especialmente adecuada en ese caso:

![ILD normalizado frente a frecuencia, azimut 5°](images/ILD_normalized_az5.png)

En estas condiciones, $`\text{ILD}_{spectrum}(f)`$ tiene una variación máxima de apenas 1.5 dB. A valores de azimut tan pequeños, el $\text{ITD}$ es aproximadamente 45 µs y el $`\text{ILD}_{avg}`$ es de apenas 2 dB —en la [nota original de RACE](https://filmaker.com/papers/RGRM-RACE_rev.pdf) se proponen un $\text{ITD}$ de 60 a 100 µs y un $`\text{ILD}_{avg}`$ de 2 a 3 dB, lo cual es coherente con un azimut pequeño.

Adicionalmente, RACE limita la cancelación recursiva a la banda entre 250 Hz y 5 kHz, dejando las frecuencias inferiores y superiores fuera del proceso de cancelación. El límite inferior reduce, entre otros efectos, los problemas asociados a la elevada energía de las cancelaciones recursivas en bajas frecuencias.

Evidentemente, desde 2006 hasta 2026 la capacidad de procesamiento en tiempo real ha aumentado enormemente, y lo que antaño no era viable mediante la computación estándar de la época hoy en día son cálculos de bajo coste. La mayor capacidad de procesamiento disponible actualmente permite a NatAmbio XTC utilizar un modelo espectral más completo y calcular filtros adecuados para azimuts moderados, con un máximo práctico alrededor de 30°, ofreciendo así un margen de aplicación mayor que RACE.

¿Es posible que incluso para NatAmbio la mejor ubicación siga siendo la de azimuts de 5° o menos? Dado que NatAmbio se ha desarrollado en un entorno doméstico y no dispone de amplias estadísticas de uso, ahora mismo no se puede ni afirmar ni desmentir esa hipótesis. No obstante, sí que hay alguna consideración que merece la pena proponer, aunque no sea como evidencia contrastada.

Una de las críticas más extendidas sobre XTC en general, y sobre RACE en particular, era que, aunque ampliaba enormemente la escena sonora virtual, lo hacía a costa de una cierta coloración. Esta queja puede explicarse, al menos en parte, por el hecho de trabajar con valores de $`\text{ILD}_{avg}`$ tan bajos como 2 dB.

Si el camino cruzado relativo tiene magnitud aproximada

```math
|G| \approx 10^{-\text{ILD}_{avg}/20}
```

entonces, con 2 dB, su magnitud es aproximadamente $|G| \approx 0.79$, mientras que con 10 dB es de $|G| \approx 0.32$. En una estructura recursiva esta diferencia es especialmente significativa, ya que los términos sucesivos de la cancelación contienen potencias crecientes de $G$. A modo ilustrativo, $0.79^{5} \approx 0.31$, mientras que $0.32^{5} \approx 0.0034$. Por tanto, con valores bajos de $`\text{ILD}_{avg}`$ los términos retardados mantienen una energía apreciable durante muchas más iteraciones, aumentando la profundidad del filtrado en peine y la coloración espectral potencial.

En las pruebas realizadas con NatAmbio, esta dependencia matemática coincide además con la percepción subjetiva: al aumentar el $`\text{ILD}_{avg}`$ utilizado en el modelo disminuyen los cambios tonales asociados a la cancelación.

Si únicamente se considerase la reducción de la energía necesaria para la cancelación, podría parecer que aumentar indefinidamente el azimut sería siempre favorable y que, por lo tanto, la mejor disposición de altavoces sería la de $\Theta = 90^{\circ}$. En este sentido hay que recordar una característica perceptual importante de un XTC correctamente ajustado: la imagen central virtual puede quedar considerablemente mejor definida que en reproducción estéreo convencional, al reducirse la contribución de la diafonía acústica. Sin embargo, esta ventaja también depende de la geometría: al aumentar excesivamente el azimut de los altavoces, mantener una imagen central estable y precisa se vuelve progresivamente más difícil.

En resumen, NatAmbio tiene vocación de solución práctica y de amplio margen de aplicación. No pretende optimizar de forma aislada la máxima apertura de escena, el máximo enfoque central o la mínima coloración, sino buscar una región de compromiso en la que apertura, enfoque, estabilidad tonal y tolerancia geométrica resulten simultáneamente satisfactorios. Esto permite mantener una elevada flexibilidad de instalación utilizando configuraciones y equipamiento habituales en sistemas de audio doméstico.

Queda por determinar experimentalmente si, incluso disponiendo de un modelo XTC más completo, existe todavía una ventaja perceptual sistemática en reducir el azimut hacia configuraciones Ambiodipole. NatAmbio no dispone todavía de una población de medidas y escuchas suficiente para responder a esta cuestión. Por ahora, los resultados sugieren más bien la existencia de una región de compromiso que la de un único azimut óptimo.

## El papel de las reflexiones tempranas en el ajuste del ILD

Una de las principales dificultades para conseguir un efecto XTC óptimo es la presencia de reflexiones acústicas tempranas de la sala o del entorno. Estas reflexiones alcanzan los oídos del oyente con diferentes retardos, niveles y ángulos de incidencia, introduciendo caminos adicionales que no coinciden con el modelo de diafonía correspondiente al sonido directo. Las reflexiones laterales resultan especialmente relevantes, ya que pueden introducir componentes cruzadas que reducen la efectividad perceptual de la cancelación.

Durante las pruebas de diseño y validación básica de NatAmbio XTC se ha observado que el valor de $`\text{ILD}_{avg}`$ que proporciona mejores resultados perceptuales tiende a ser algo superior al estimado a partir de las bases de datos HRTF analizadas —del orden de 4 dB por encima, según se recoge en el [ejemplo de la nota principal](xtc_filters_es.md#ejemplo-de-filtros-obtenidos-por-este-nuevo-algoritmo). Aunque este comportamiento requiere todavía validación experimental específica, se propone como posible explicación la influencia de las reflexiones tempranas:

- Los valores bajos de $`\text{ILD}_{avg}`$ requieren señales de cancelación de mayor energía. Estas señales no se propagan únicamente por el camino acústico que se pretende cancelar, sino que también generan sus propias reflexiones tempranas. Parte de la ventaja observada al utilizar valores de $`\text{ILD}_{avg}`$ superiores a los derivados directamente de las HRTF podría proceder de la reducción de esta energía acústica adicional: una cancelación menos intensa produciría también componentes reflejadas de menor energía, reduciendo la posibilidad de que estas interfieran con la localización buscada.

- En el caso del $\text{ITD}$, por el contrario, los valores estimados a partir de las bases de datos HRTF utilizadas han proporcionado un ajuste adecuado sin necesidad de introducir una corrección equivalente. Esto resulta coherente con el objetivo del modelo: el retardo de NatAmbio XTC se ajusta para que la señal de cancelación coincida temporalmente con la diafonía vinculada al sonido directo. Las componentes de diafonía asociadas a las reflexiones tempranas llegan posteriormente y con retardos diversos, por lo que intentar incorporarlas mediante una modificación del $\text{ITD}$ único del modelo desplazaría la cancelación respecto al camino que se pretende cancelar prioritariamente.

Sería muy complejo realizar un estudio detallado del impacto de las reflexiones tempranas sobre la diafonía considerando sus diferentes retardos, niveles y ángulos de llegada a los oídos del oyente. En lugar de intentar incorporar explícitamente esta complejidad al modelo, NatAmbio utiliza los valores derivados de las HRTF como referencia inicial y permite el ajuste independiente de $\text{ITD}$ e $`\text{ILD}_{avg}`$. En la experiencia obtenida hasta ahora, el $\text{ITD}$ calculado constituye una referencia temporal suficientemente estable, mientras que el $`\text{ILD}_{avg}`$ actúa como principal parámetro de adaptación a las condiciones acústicas de la sala.

## Propuesta de método de ajuste para NatAmbio XTC

Desarrollado el modelo de $G$ en NatAmbio XTC y explicada su implementación, es el momento de proponer un método práctico para determinar y ajustar los cuatro parámetros implicados: $\text{ITD}$, $`\text{ILD}_{avg}`$, $\Theta$ y $\alpha$.

En primer lugar es necesario medir el azimut $\Theta$ real del sistema de sonido. Es importante destacar que este azimut corresponde a la mitad del ángulo total de apertura entre los altavoces respecto al oyente. Por ejemplo, una disposición estéreo estándar con 60° de apertura corresponde a un azimut $\Theta = 30°$.

Obtenida esa medida física, mediante las regresiones propuestas se pueden definir unos primeros valores de $\text{ITD}$ e $`\text{ILD}_{avg}`$. Y para la fórmula empírica de $`\text{ILD}_{spectrum}(f)`$ se puede partir del $\Theta$ medido y de un $\alpha$ de 1.8.

Configurado de partida el sistema, el primer paso es el ajuste del $`\text{ILD}_{avg}`$. Sea con música bien conocida o con la síntesis de señales de prueba a partir de los scripts [`testing_XTC`](../../tools/testing_XTC/README.md), se trata de conseguir una amplia escena sonora sin introducir coloración tonal perceptible. Este ajuste puede realizarse en pasos de 1 o 2 dB: la realidad acústica de un sistema de audio doméstico no es tan sensible como para requerir ajustes muy finos. Igualmente, es habitual que, dentro de un margen de aproximadamente $\pm 1$ dB alrededor del óptimo, las diferencias percibidas sean muy sutiles o directamente inapreciables.

El ajuste de $\alpha$ se realiza con música o con alguna señal sonora que incluya registro agudo fuertemente paneado (por ejemplo, percusión o una batería). Se trata de alcanzar un valor de $\alpha$ con el que el agudo se perciba muy lateral, pero sin una sensación de "brillo" o "aura" en el canal contralateral. Es cierto que es una explicación un tanto imprecisa, pero en el ajuste de casos reales la sensación se hace patente. Al igual que en el caso del $`\text{ILD}_{avg}`$, la sensibilidad a $\alpha$ no es muy alta: pasos inferiores a 0.1–0.2 no resultan prácticos, y habitualmente existe un intervalo alrededor del valor óptimo en el que las diferencias resultan muy sutiles o inapreciables.

Concluidos estos pasos, quizás alguien quiera valorar cuál es la sensibilidad al $\text{ITD}$ en su sistema, comprobando el efecto de pequeñas variaciones, pero el resultado principal ya se ha alcanzado.

En aquellos sistemas en los que la apertura lateral de las fuentes sonoras virtuales sea asimétrica, hay que recurrir al [modelo asimétrico de XTC](xtc_no_simetrico_es.md). El modelo y la parametrización son idénticos, pero se opera con dos juegos de parámetros distintos, uno para cada canal. Una propuesta práctica inicial para este caso consiste en copiar los valores obtenidos en el ajuste simétrico y comenzar aumentando ligeramente el $`\text{ILD}_{avg}`$ correspondiente al lado que presenta menor apertura de escena. Esta recomendación parte de la hipótesis expuesta anteriormente: si la asimetría está relacionada con una mayor contribución de reflexiones tempranas en ese lado, reducir la energía de cancelación puede disminuir también la excitación de esos caminos reflejados. Debe considerarse, por tanto, un punto de partida experimental y no una regla general.

Llegados a un ajuste satisfactorio, es conveniente no prolongar innecesariamente las pruebas. Resulta preferible disfrutar durante un tiempo de la escucha habitual de música y, tras un periodo de aclimatación, volver si se desea al ajuste fino. El objetivo no es encontrar una combinación matemática única de parámetros, sino una región de funcionamiento estable en la que apertura de escena, localización y equilibrio tonal resulten satisfactorios.

## Notas

[^1]: El ITD se ha obtenido como el retardo aplicado al canal ipsilateral que maximiza la correlación entre ambos canales, promediando los valores obtenidos a azimut Θ y 180° − Θ. El ILD promedio es el cociente entre las energías de ambas impulsivas. El espectro de ILD se ha obtenido mediante la convolución de las impulsivas por un banco de filtros gammatone con un salto de 1 ERB entre ellos (modelo de Glasberg y Moore), su promediado por el ancho de banda de cada paso ERB y, finalmente, el cociente entre canal ipsilateral y canal contralateral.

## Referencias

**Diafonía / XTC**

1. Glasgal, R. & Miller, R. (Robin). *Recursive Ambiophonic Crosstalk Elimination (RACE)*. Ambiophonics Institute / Filmaker Technology. <https://filmaker.com/papers/RGRM-RACE_rev.pdf>

**Modelos auditivos**

2. Glasberg, B. R. & Moore, B. C. J. (1990). Derivation of auditory filter shapes from notched-noise data. *Hearing Research* 47(1–2), 103–138. <https://doi.org/10.1016/0378-5955(90)90170-T> (Banco de filtros gammatone y escala ERB empleados en el cálculo de $`\text{ILD}_{spectrum}`$.)

**Conjuntos de datos HRTF**

3. Brinkmann, F., Dinakaran, M., Pelzer, R., Wohlgemuth, J. J., Seipel, F., Voss, D., Grosche, P. & Weinzierl, S. (2019). *The HUTUBS head-related transfer function (HRTF) database*. Technische Universität Berlin. <https://doi.org/10.14279/depositonce-8487>
4. Watanabe, K., Iwaya, Y., Suzuki, Y., Takane, S. & Sato, S. (2014). Dataset of head-related transfer functions measured with a circular loudspeaker array. *Acoustical Science and Technology* 35(3), 159–165. (Base de datos RIEC, Tohoku University.) <https://www.riec.tohoku.ac.jp/pub/hrtf/>
5. Carpentier, T., Bahu, H., Noisternig, M. & Warusfel, O. (2014). Measurement of a Head-Related Transfer Function Database with High Spatial Resolution. *7th Forum Acusticum*. (Base de datos BiLi, IRCAM.)
6. Algazi, V. R., Duda, R. O., Thompson, D. M. & Avendano, C. (2001). The CIPIC HRTF database. *Proc. 2001 IEEE Workshop on Applications of Signal Processing to Audio and Acoustics (WASPAA)*, 99–102. <https://doi.org/10.1109/ASPAA.2001.969552>
7. Majdak, P., Balazs, P. & Laback, B. (2007). Multiple exponential sweep method for fast measurement of head-related transfer functions. *J. Audio Eng. Soc.* 55(7/8), 623–637. (Método de medida de la base de datos ARI, Acoustics Research Institute, Austrian Academy of Sciences.)
8. Repositorio de bases de datos en formato SOFA. <https://sofacoustics.org/data/database/> — información descriptiva en <https://www.sofaconventions.org/mediawiki/index.php/Files>
