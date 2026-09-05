# nae_erb — cuaderno de trabajo

Notas internas de la implementación, no documentación de usuario. Registro de
decisiones, medidas y trampas encontradas, para no repetir el camino y para
tener el material del paper técnico cuando toque.

## Qué es

NAE con una PCA por banda ERB en lugar de una sola sobre todo el espectro. La
única variación conceptual es `V(t) → V_b(t)`. Documentos de partida:
`~/Descargas/NAE_PCA_bandas_ERB_gammatone.md` y la revisión
`~/Descargas/NAE_PCA_ERB_limitada_resolucion_temporal.md`.

## Las dos ideas que lo hacen viable en tiempo real

**El banco no toca el audio.** Dos identidades, ambas verificadas a 9e-16:

- la covarianza de la banda `b` sobre la ventana es el espectro de la propia
  ventana pesado por `W_b(f)²` (Parseval), así que cada eje sale de **una**
  transformada directa, sin inversa y sin retardo;
- cada banda reconstruye con un proyector constante `P_b = v_b v_bᵀ` aplicado a
  `W_b(f)X(f)`, luego la suma sobre bandas es **una** matriz 2×2 dependiente de
  la frecuencia, `G1(f) = Σ_b W_b(f) P1_b`, con `G1 + G2 = I`.

El número de bandas no entra en el número de transformadas.

**El overlap-add se calcula al revés.** Aplicar un filtro es lineal en el
filtro, así que sumar los proyectores de las últimas `covsteps` ventanas y
aplicar una vez es lo mismo que aplicar cada uno y sumar — que es lo que hace el
overlap-add de `nae.cpp`. Hacerlo así permite sintetizar **en el momento de
emitir**, cuando la trama ya tiene `(covsteps-1)·sample_count` muestras de
contexto a su derecha.

## Tres ventanas

Todas terminan en la muestra recién llegada.

| ventana | qué es | coste |
|---|---|---|
| reconstrucción | `covsteps · sample_count`, la de NAE | intacta, latencia sin cambios |
| análisis | `<cov_window_ms>`, 64 ms | ninguno: son muestras ya reproducidas |
| síntesis | igual que la de análisis | ninguno, misma razón |

## Medidas que sostienen los valores por defecto

Sobre `manne_his_men_i_am_in_love.wav` (30 s, el caso lateralizado del §17),
mismo banco (`band_min` 125 Hz, ΔERB 2, 20 bandas), variando solo el análisis:

| análisis | Δt mediana | q<1.5 | corr C2 vs broadband |
|---|---|---|---|
| 16 ms | 9.65° | 3.6 % | 0.528 |
| 32 ms | 6.10° | 4.8 % | 0.497 |
| **64 ms** | **3.65°** | 5.7 % | 0.484 |
| 128 ms | 1.72° | 6.4 % | 0.468 |

Referencias: broadband 3.34°, ideal no realizable (`--filtering whole`) 3.24°.
Los 64 ms son donde el jitter iguala al broadband sin pasarse; con 128 ms el eje
se vuelve *más* quieto que el broadband, que es señal de estar promediando de
más y responder despacio a un cambio de escena.

## El artefacto que las métricas no vieron

Un producto en frecuencia es una convolución circular. Aplicado sobre
exactamente la ventana de reconstrucción no hay holgura: la cola se pliega sobre
la cabeza, que es justo la trama que se emite. **Se cancela exactamente entre C1
y C2** (porque `G1 + G2 = I`), así que cualquier control sobre la suma lo da por
bueno — el de reconstrucción pasaba a 1e-16 mientras el alias estaba **7.9 dB
por encima de C1 entre 10 y 20 kHz**.

Lo detectó el oído antes que ninguna medida. La métrica que faltaba compara
**cada componente** contra una síntesis mucho más larga, no la suma contra la
entrada. Doblar la transformada lo deja en −53 dB y satura ahí.

Lección para el resto del proyecto: un control que solo mira `C1 + C2` es ciego
a todo lo que sea antisimétrico entre las dos componentes.

## Estado del código

| pieza | dónde | estado |
|---|---|---|
| motor offline de referencia | `tools/python_nae_natambio/nae_natambio_erb.py` | validado y escuchado |
| banco de pruebas | `tools/nae_bench/` | `--check` determinista |
| refactor de `NAE` en 4 fases | `src/nae.cpp`, `src/nae.hpp` | bit a bit idéntico al anterior |
| `NaeErb` | `src/nae_erb.{hpp,cpp}` | −77.4 dB de la referencia offline |
| tag `<nae_erb>` | `naconf.cpp`, `structs.hpp`, `natambio.cpp` | probado de extremo a extremo |
| informe remoto | `remote.cpp`, `iojack.cpp` | `naeget` y `getxmlconfig` correctos |

## Carga

Medido con `timecycle` contra jackd a 256 tramas, un engine alpha:

- primera versión: 1396 µs por periodo de 5333 → **26.2 %**. Mi estimación de
  diseño decía ~1 %: me dejé el factor `covsteps` en el ensamblado de `G` y los
  accesos a las máscaras iban a contrapelo de la caché.
- tras sumar el anillo antes de tocar las máscaras y almacenar el banco
  bin-major: 625 µs → **11.7 % de media, 23.5 % de pico**.

El pico es el número que provoca xruns, no la media.

Queda margen sin explotar: dar a cada banda el rango de bins donde realmente
pesa recortaría otro ~5×, pero trunca la partición de la unidad y la
reconstrucción deja de ser exacta. No tomado.

## Trampas encontradas (y por las que no volver a pasar)

1. **`--disable-dependency-tracking`.** El árbol viene configurado por
   `dpkg-buildpackage` y no reconstruye al cambiar una cabecera. Tocar un `.hpp`
   y hacer `make` da un binario con layouts de clase mezclados que aborta con
   `malloc(): invalid size`. Siempre `make clean`.
2. **Destructor.** El worker está dentro de `decompose()`, que es virtual de la
   derivada. `~NaeErb()` para y hace join **antes** de nada; si lo dejara a
   `~NAE()`, el hilo leería una banda ya liberada. Y `~NAE()` tuvo que hacerse
   virtual porque `ioJack` borra a través de `vector<NAE*>`.
3. **`findNaeNode` contaba solo nodos `"nae"`.** Con `naelist` conteniendo los
   dos tipos en orden de documento, un `<nae_erb>` antes de un `<nae>`
   desincronizaba el índice y escribía la ganancia en la estructura equivocada.
4. **FFTW.** Planificar asigna memoria y no es thread-safe consigo misma: todo
   en `load()`, que por eso es virtual. En el bloque solo `fftw_execute`.
5. **Denormales.** Las máscaras caen como f⁻⁴ y los productos espectrales se
   hunden en denormales en cualquier pasaje silencioso. FTZ/DAZ en el bloque.
6. **El OLA del script Python no es el de `nae.cpp`.** Con eje fijo difieren en
   0.59 sobre un RMS de 1.0. `nae.cpp` acumula cada ranura sobre `covsteps`
   ventanas y emite la más vieja entre `covsteps+1`; el script suma su
   acumulador entero cada ventana y lo arrastra dividido por `covsteps`. El modo
   `matrix` del script sigue al motor, no al script.
7. **Beta.** `nae.cpp` escribe `side_weight` solo en la trama que entra; las
   viejas conservan el peso con que se escribieron. El script Python
   remultiplica toda la cola cada ventana, así que en beta **compone** el
   escalado. `NaeErb` sigue a `nae.cpp`.

## Pendiente

- Escucha en tiempo real en panambio01 (FFADO, 256 tramas). **Es la prueba de
  fuego y está sin hacer.**
- Documentación de usuario (`README.CONFIG`, `architecture.md`, `src/README.md`)
  y changelog.
- Bump a 2.0.0 en la consolidación.
- Paper técnico, juntando esto con los markdown de estudio de referencias.
