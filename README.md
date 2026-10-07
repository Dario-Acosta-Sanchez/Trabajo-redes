# Práctica 2 de Redes: Dominio Territorial

Juego cliente/servidor sobre **TCP**. El servidor atiende a varios clientes a la vez con **`select()`**.
Está construido sobre los ejemplos de clase (`ejemploServidorChat.c` y `ejemploClienteChat.c`).

---

## 1. Ficheros

| Fichero | Qué contiene |
|---|---|
| `protocolo.h` | Constantes comunes a cliente y servidor: `PUERTO 2026`, `MSG_SIZE 250`, `MAX_TEXTO 200`. |
| `servidor.c` | Parte de red del servidor (socket, bind, listen, select, accept, recv, send), estado de cada cliente, cola de espera, partidas y **todos los comandos del protocolo**. |
| `juego.h` / `juego.c` | **Reglas del juego**: tablero, conquistar, atacar, reforzar, adyacencia, recuento y dibujo del tablero. No usa sockets. |
| `usuarios.h` / `usuarios.c` | Registro y validación de usuarios. Se guardan en `usuarios.txt`. |
| `cliente.c` | Cliente de consola: manda lo que escribes y muestra lo que llega (usa `select()` sobre el teclado y el socket). |
| `usuarios.txt` | Usuarios de prueba: `jugador1` … `jugador4`, todos con la password `1234`. |
| `Makefile` | `make` compila todo y `make clean` borra los binarios. |

## 2. Compilar y probar

```bash
make
```

Terminal 1:
```bash
./servidor
```

Terminales 2 y 3 (puedes pasar la IP del servidor para jugar desde otro equipo):
```bash
./cliente
```

Ejemplo de partida:
```
USUARIO jugador1          (en el otro cliente: USUARIO jugador2)
PASSWORD 1234
INICIAR-PARTIDA
ESTADO
CONQUISTAR B2 B3 10
ATACAR B3 C3 15
REFORZAR B2 B3 2
PASAR
SALIR
```

---

## 3. Arquitectura

```
 cliente.c                         servidor.c                         juego.c / usuarios.c
 ┌──────────────┐   TCP:2026   ┌─────────────────────────┐        ┌────────────────────┐
 │ select():    │ ───────────▶ │ select() sobre:          │        │ Reglas del juego   │
 │  - teclado   │  bloques de  │  - socket de escucha     │ llama  │ (sin sockets)      │
 │  - socket    │ ◀─────────── │  - teclado (fd 0)        │ ─────▶ │ Usuarios en        │
 └──────────────┘  250 bytes   │  - socket de cada cliente│        │ usuarios.txt       │
                               └─────────────────────────┘        └────────────────────┘
```

- **El cliente no conoce las reglas.** Solo reenvía texto y lo muestra. Todo lo valida el servidor.
- **`juego.c` no sabe nada de la red.** Recibe el tablero y los argumentos, aplica las reglas y devuelve el texto `+Ok.`/`-Err.`. El servidor decide a quién enviarlo. Así la lógica y la comunicación quedan separadas.

---

## 4. La parte de red, paso a paso

### Servidor (`main` en `servidor.c`)

1. **`socket(AF_INET, SOCK_STREAM, 0)`**: crea un socket TCP (`SOCK_STREAM` = orientado a conexión).
2. **`setsockopt(SO_REUSEADDR)`**: permite reutilizar el puerto nada más cerrar el servidor. Sin esto habría que esperar a que el puerto salga de `TIME_WAIT`.
3. **`bind()`**: asocia el socket al puerto 2026 en todas las interfaces (`INADDR_ANY`). `htons()` pasa el puerto al orden de bytes de la red (big-endian).
4. **`listen()`**: convierte el socket en **pasivo**, es decir, en uno que espera conexiones. El segundo parámetro es el tamaño de la cola de conexiones pendientes.
5. **`FD_ZERO` / `FD_SET`**: forman el conjunto `readfds` con el socket de escucha y el teclado (descriptor 0).
6. **Bucle con `select()`**:
   - `select` **modifica** el conjunto que recibe, así que se le pasa una copia (`auxfds = readfds`).
   - Con `timeout = NULL` es **bloqueante**: el servidor duerme hasta que pasa algo.
   - Después se recorren los descriptores con `FD_ISSET`:
     - **Socket de escucha** → `accept()` devuelve un **socket nuevo** para hablar con ese cliente. Se añade a `readfds` con `FD_SET` y se le envía `+Ok. Usuario conectado`. Si ya hay 10 clientes, se envía `-Err. Demasiados clientes conectados` y se cierra.
     - **Descriptor 0 (teclado)** → si el administrador escribe `SALIR`, se cierra el servidor.
     - **Socket de un cliente** → `recv()`:
       - `> 0`: hay un mensaje y se procesa (`procesarMensaje`).
       - `0`: el cliente cerró la conexión (por ejemplo con Ctrl+C) y se le da de baja (`salirCliente`).
       - `-1`: error, y también se le da de baja.
7. **Cierre**: `close()` de todos los sockets.

### Cliente (`cliente.c`)

`socket()`, después `connect()` (aquí se establece la conexión TCP) y luego un bucle con `select()` sobre el **teclado y el socket**.
Hace falta `select()` porque el servidor puede escribirnos en cualquier momento, aunque no hayamos tecleado nada (por ejemplo "Empieza la partida" o la jugada del rival).
Si con `fgets` nos quedáramos bloqueados esperando teclado, no veríamos esos mensajes.

### Señales

- **SIGINT (Ctrl+C) en el servidor**: el manejador solo activa una bandera (`finServidor`). Dentro de un manejador de señal solo se deben hacer cosas muy simples. `select` se interrumpe con `errno = EINTR`, el bucle ve la bandera y hace un cierre ordenado: envía `+Ok. Desconexión servidor` a todos y cierra los sockets.
- **SIGPIPE ignorada** (en servidor y cliente): si hacemos `send()` a un socket que el otro extremo ya cerró, el sistema envía SIGPIPE, y por defecto **mata el proceso**. Al ignorarla, `send` simplemente devuelve -1.

---

## 5. Formato de los mensajes (sintaxis)

- Cada mensaje viaja como un **bloque fijo de 250 bytes** (`MSG_SIZE`): el texto va al principio y el resto se rellena con `'\0'`. Es lo mismo que hacen los ejemplos (`send(sd, buffer, sizeof(buffer), 0)`).
- **¿Por qué tamaño fijo?** TCP es un **flujo de bytes** y no conserva los límites entre mensajes. Si todos los bloques miden lo mismo, el receptor sabe dónde empieza cada mensaje.
- El texto útil mide como mucho **200 caracteres** (`MAX_TEXTO`, lo exige el enunciado).
- **Mensajes de tamaño arbitrario**: el tablero ocupa unos 550 caracteres. `enviarTextoLargo()` lo **trocea en varios mensajes de ≤ 200 caracteres** y corta siempre al final de una línea. El cliente se limita a imprimir los trozos uno detrás de otro, así que el tablero se ve completo.
- `send()` puede enviar menos bytes de los pedidos, por eso `enviarMensaje()` repite el envío hasta completar el bloque.
- El cliente usa `recv(..., MSG_WAITALL)`, que no vuelve hasta tener el bloque completo de 250 bytes.
- Todas las respuestas del servidor empiezan por `+Ok.` (petición aceptada) o `-Err.` (error).

---

## 6. Estados de un cliente en el servidor (semántica según el contexto)

```
   CONECTADO ──USUARIO ok──▶ USUARIO_OK ──PASSWORD ok──▶ VALIDADO
                                                          │   ▲
                                          INICIAR-PARTIDA │   │ fin de partida
                                                          ▼   │ o el rival sale
                              ESPERANDO ──llega rival──▶ JUGANDO
```

Antes de ejecutar un comando, el servidor mira el estado del cliente. Así se cumplen las restricciones del enunciado:

| Comando | Solo vale en | Si no, responde |
|---|---|---|
| `USUARIO u` | CONECTADO / USUARIO_OK | `-Err. Ya estás validado` |
| `PASSWORD p` | USUARIO_OK | `-Err. Primero debes indicar el usuario…` |
| `REGISTRO u p` | sin validar | `-Err. Ya estás validado…` |
| `INICIAR-PARTIDA` | VALIDADO | `-Err. Debes validarte…` / `Ya estás esperando` / `Ya estás jugando` |
| `ESTADO` | JUGANDO (también fuera de tu turno) | `-Err. No estás dentro de ninguna partida` |
| `CONQUISTAR` / `ATACAR` / `REFORZAR` / `PASAR` | JUGANDO **y en tu turno** | `-Err. No es tu turno` |
| `SALIR` | siempre | — |
| cualquier otro | — | `-Err. Comando no reconocido` |

Un número incorrecto de argumentos produce `-Err. Formato incorrecto. Uso: …`.
El servidor comprueba primero la **sintaxis** (formato del mensaje) y después la **semántica** (si tiene sentido en ese momento).

---

## 7. Protocolo completo

| Petición del cliente | Respuestas posibles |
|---|---|
| *(conexión)* | `+Ok. Usuario conectado` · `-Err. Demasiados clientes conectados` |
| `USUARIO usuario` | `+Ok. Usuario correcto` · `-Err. Usuario incorrecto` |
| `PASSWORD password` | `+Ok. Usuario validado` · `-Err. Error en la validación` |
| `REGISTRO usuario password` | `+Ok. Usuario registrado…` · `-Err. Ya existe un usuario con ese nombre` |
| `INICIAR-PARTIDA` | `+Ok. Esperando otro jugador` · `+Ok. Empieza la partida` (a los dos) |
| `ESTADO` | `+Ok. Estado de la partida…` + tablero |
| `CONQUISTAR B2 B1 10` | `+Ok. Territorio B1 conquistado.` |
| `ATACAR C3 B3 10` | `+Ok. Has conquistado el territorio B3.` · `+Ok. Has perdido el ataque contra B3.` |
| `REFORZAR B2 B3 2` | `+Ok. Territorio reforzado.` |
| `PASAR` | `+Ok. Has pasado el turno.` |
| `SALIR` | `+Ok. Desconexión procesada` (el rival recibe `+Ok. Tu oponente ha terminado la partida`) |

Después de cada acción válida, el **rival recibe un aviso** ("Tu oponente ha conquistado…", "te ha atacado…") y los dos reciben de quién es el turno.

---

## 8. Reglas y decisiones de diseño (por si te preguntan "¿por qué…?")

1. **Empate en un ataque (atacantes = defensores)**: gana el **defensor**. El enunciado solo dice que el atacante conquista si sus unidades son **superiores**.
2. **10 turnos máximos**: un turno es la acción de un jugador, así que hay **5 acciones por jugador**. Para cambiarlo basta con tocar `MAX_TURNOS` en `juego.h`.
3. **Unidades**: se pueden usar **todas** las del origen, porque el enunciado solo prohíbe usar *más* de las disponibles. El territorio de origen sigue siendo tuyo aunque se quede con 0 unidades, y cualquier ataque con al menos 1 unidad lo conquista.
4. **Quién empieza**: J1, que es el jugador que llevaba más tiempo esperando.
5. **Territorios iniciales**: se eligen al azar con `rand()` y siempre son distintos.
6. **Información oculta**: con `ESTADO` ves las unidades de tus territorios. De los del rival solo ves que son suyos. Al final de la partida se muestra el tablero completo.
7. **Fin de partida**: al llegar a 10 turnos o cuando un jugador se queda sin territorios. Gana quien tiene más territorios y, si empatan, quien tiene más unidades. Si también empatan en unidades, es empate.
8. **Máximo 4 partidas**: si ya hay 4 en juego, los jugadores esperan en una **cola FIFO** (`colaEspera`). Cuando termina una partida, `emparejarJugadores()` empareja a los dos primeros de la cola.
9. **Un mismo usuario no puede tener dos sesiones a la vez**: así nadie puede jugar contra sí mismo.
10. **REGISTRO no inicia sesión**: después hay que hacer `USUARIO` y `PASSWORD`.
11. **Usuarios persistentes** en `usuarios.txt`, con el formato `usuario password` por línea. Las contraseñas se guardan en claro, lo cual es aceptable en una práctica pero no en un sistema real.
12. Se aceptan los territorios en minúscula (`b3` = `B3`), pero los **comandos van en mayúsculas**, tal como los define el protocolo.

---

## 9. Mapa de funciones de `servidor.c`

| Función | Para qué sirve |
|---|---|
| `main` | Socket, bind, listen y bucle de `select` (mismo esqueleto que el ejemplo del chat). |
| `procesarMensaje` | Trocea el mensaje con `sscanf` y llama al comando que corresponda. |
| `comandoUsuario` / `comandoPassword` / `comandoRegistro` | Login y registro. |
| `comandoIniciarPartida` | Mete al jugador en la cola y llama a `emparejarJugadores`. |
| `emparejarJugadores` | Si hay 2 o más esperando y una partida libre, llama a `iniciarPartida`. |
| `iniciarPartida` | Prepara el tablero y avisa a los dos jugadores. |
| `comandoJuego` | Para ESTADO, CONQUISTAR, ATACAR, REFORZAR y PASAR: comprueba estado, formato y turno, y llama a `juego.c`. |
| `finalizarTurno` | Suma el turno, comprueba si la partida acaba y cambia de jugador. |
| `finalizarPartida` | Hace el recuento, anuncia el ganador, muestra el tablero final y deja a los jugadores en VALIDADO. |
| `salirCliente` | Da de baja a un cliente (por SALIR o por desconexión). Avisa al rival, cierra el socket y hace `FD_CLR`. |
| `enviarMensaje` / `enviarTextoLargo` | Envían bloques de 250 bytes y trocean los textos largos. |
| `manejador` / `cerrarServidor` | Gestionan Ctrl+C y el cierre ordenado. |

---

## 10. Preguntas típicas de la defensa

- **¿Por qué TCP y no UDP?** El juego necesita que los comandos lleguen todos, sin errores y en orden. TCP lo garantiza (retransmisiones, numeración y control de flujo). Con UDP tendríamos que implementar todo eso nosotros.
- **¿Qué hace `select()` y por qué no usar un proceso por cliente?** Vigila varios descriptores a la vez y avisa de cuál tiene datos. Cada petición del juego se atiende en microsegundos, así que un único proceso basta y además no hay que compartir el estado de las partidas entre procesos. Es la opción que pide el enunciado.
- **¿Por qué `auxfds = readfds` antes de cada `select`?** Porque `select` sobrescribe el conjunto y deja marcados solo los descriptores que tienen actividad.
- **¿Qué devuelve `accept()`?** Un socket **nuevo** conectado con ese cliente. El de escucha sigue aceptando conexiones de otros.
- **¿Cómo sabe el servidor que un cliente se ha ido sin enviar SALIR?** `recv()` devuelve 0. Entonces se ejecuta `salirCliente()`, que también avisa a su rival.
- **¿Qué pasa si `recv` devuelve menos de 250 bytes?** En el cliente no puede ocurrir porque se usa `MSG_WAITALL`. En el servidor se hace un único `recv` por aviso de `select`, como en el ejemplo, para no bloquear a los demás clientes. Los mensajes son pequeños, así que en la práctica llegan enteros. Si llegara un trozo vacío (solo relleno de `'\0'`), se ignora.
- **¿Para qué sirve `SO_REUSEADDR`?** Para poder relanzar el servidor en el mismo puerto sin esperar a que pase `TIME_WAIT`.
- **¿Para qué sirve `htons`?** Para pasar el puerto al orden de bytes de la red (big-endian), que puede no coincidir con el del procesador.
- **¿Cómo se controla que no se pasen de 200 caracteres?** El cliente no envía líneas de más de 200 caracteres. El servidor rechaza mensajes de más de 200 y nunca escribe más de 200 en un mensaje: usa `snprintf` con límite y trocea los textos largos.
- **¿Cómo se comprueba la adyacencia?** Con la distancia Manhattan: `|f1-f2| + |c1-c2| == 1`. Esto excluye las diagonales (distancia 2) y la propia casilla (distancia 0).
