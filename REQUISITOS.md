# Requisitos y Casos de Uso — Dominio Territorial

Documento de análisis para la Práctica 2 de Redes (sockets TCP). Basado en el enunciado de la práctica.

## 1. Descripción general

Aplicación cliente/servidor en C que implementa el juego **Dominio Territorial**: dos jugadores compiten por controlar el mayor número de territorios de un tablero 5×5, usando TCP como protocolo de transporte y un protocolo de aplicación propio basado en texto.

## 2. Actores

- **Jugador (Cliente):** se conecta al servidor, se identifica, solicita partida y envía acciones de juego.
- **Servidor:** acepta conexiones, valida usuarios, empareja jugadores, gestiona el estado de cada partida y aplica las reglas del juego.

## 3. Requisitos funcionales

| ID | Requisito |
|----|-----------|
| RF-01 | El servidor debe aceptar conexiones de clientes vía TCP en el puerto **2026**. |
| RF-02 | Al conectar, el servidor debe responder `+Ok. Usuario conectado`. |
| RF-03 | El cliente debe poder registrarse con `REGISTRO [usuario] [password]`, evitando colisiones con usuarios ya existentes. |
| RF-04 | El cliente debe poder identificarse con `USUARIO [usuario]` seguido de `PASSWORD [password]`, validando ambos contra el sistema. |
| RF-05 | Un usuario validado debe poder solicitar partida con `INICIAR-PARTIDA`. |
| RF-06 | El servidor debe emparejar automáticamente a dos jugadores en espera y avisar a ambos con `+Ok. Empieza la partida`; si no hay pareja, responde `+Ok. Esperando otro jugador`. |
| RF-07 | Al iniciar partida, el servidor debe generar un tablero 5×5, asignar a cada jugador un territorio inicial aleatorio distinto con 30 unidades, y dejar el resto neutral sin unidades. |
| RF-08 | El cliente debe poder consultar el estado del tablero con `ESTADO`, viendo unidades propias y solo el propietario (sin unidades) de los territorios ajenos. |
| RF-09 | El cliente debe poder conquistar un territorio neutral adyacente con `CONQUISTAR [origen] [destino] [unidades]`. |
| RF-10 | El cliente debe poder atacar un territorio enemigo adyacente con `ATACAR [origen] [destino] [unidades]`. |
| RF-11 | El cliente debe poder reforzar un territorio propio adyacente con `REFORZAR [origen] [destino] [unidades]`. |
| RF-12 | El cliente debe poder pasar turno sin acción con `PASAR`. |
| RF-13 | El cliente debe poder desconectarse con `SALIR`, liberando su plaza o finalizando su partida en curso (avisando al rival). |
| RF-14 | El servidor debe validar cada acción (existencia de territorios, adyacencia, propiedad, turno, unidades suficientes) antes de aplicarla. |
| RF-15 | Resuelto un ataque, el bando con más unidades gana el territorio; el perdedor pierde las unidades empleadas/existentes. |
| RF-16 | Tras una acción válida, el turno pasa al otro jugador. |
| RF-17 | La partida finaliza al alcanzar el **máximo de turnos (10)** o cuando un jugador pierde todos sus territorios. |
| RF-18 | Al finalizar, el servidor determina el ganador por nº de territorios y, en caso de empate, por nº total de unidades. |
| RF-19 | Cualquier mensaje mal formado o no reconocido debe generar `-Err. <motivo>`. |

## 4. Requisitos no funcionales

| ID | Requisito |
|----|-----------|
| RNF-01 | Comunicación orientada a conexión (**TCP**). |
| RNF-02 | Servidor **concurrente** implementado con `select()` (no procesos/hilos por cliente). |
| RNF-03 | Máximo **10 clientes** conectados simultáneamente. |
| RNF-04 | Máximo **4 partidas** simultáneas. |
| RNF-05 | Máximo **10 turnos** por partida. |
| RNF-06 | Mensajes de longitud arbitraria con tope de **200 caracteres**. |
| RNF-07 | Interacción por consola en el cliente. |
| RNF-08 | Respuestas del servidor con prefijo `+Ok.` (éxito) o `-Err.` (error) seguido de texto informativo. |
| RNF-09 | Adyacencia solo ortogonal (arriba/abajo/izquierda/derecha); la diagonal no cuenta. |

## 5. Protocolo de mensajes (resumen)

| Mensaje | Dirección | Descripción |
|---|---|---|
| `USUARIO [usuario]` | Cliente→Servidor | Identifica al usuario. |
| `PASSWORD [password]` | Cliente→Servidor | Envía la contraseña del usuario. |
| `REGISTRO [usuario] [password]` | Cliente→Servidor | Da de alta un usuario nuevo. |
| `INICIAR-PARTIDA` | Cliente→Servidor | Solicita entrar en una partida. |
| `ESTADO` | Cliente→Servidor | Solicita el estado del tablero. |
| `CONQUISTAR [origen] [destino] [unidades]` | Cliente→Servidor | Conquista territorio neutral. |
| `ATACAR [origen] [destino] [unidades]` | Cliente→Servidor | Ataca territorio enemigo. |
| `REFORZAR [origen] [destino] [unidades]` | Cliente→Servidor | Mueve unidades entre territorios propios. |
| `PASAR` | Cliente→Servidor | Termina el turno sin acción. |
| `SALIR` | Cliente→Servidor | Desconecta del servicio. |
| `+Ok. <texto>` / `-Err. <texto>` | Servidor→Cliente | Respuesta de éxito / error. |

## 6. Casos de uso

### UC-01 Registrar usuario
- **Actor:** Jugador
- **Precondición:** el cliente está conectado al servidor.
- **Flujo principal:**
  1. El cliente envía `REGISTRO [usuario] [password]`.
  2. El servidor comprueba que el nombre de usuario no existe.
  3. El servidor da de alta al usuario y responde `+Ok.`.
- **Flujo alternativo:** el usuario ya existe → `-Err. Usuario ya registrado`.
- **Postcondición:** el usuario queda registrado y puede identificarse.

### UC-02 Identificarse (login)
- **Actor:** Jugador
- **Precondición:** el cliente está conectado.
- **Flujo principal:**
  1. Cliente envía `USUARIO [usuario]`.
  2. Servidor responde `+Ok. Usuario correcto` si existe, o `-Err. Usuario incorrecto`.
  3. Cliente envía `PASSWORD [password]`.
  4. Servidor responde `+Ok. Usuario validado` o `-Err. Error en la validación`.
- **Postcondición:** el usuario queda autenticado y puede solicitar partida.

### UC-03 Iniciar partida
- **Actor:** Jugador
- **Precondición:** usuario autenticado.
- **Flujo principal:**
  1. Cliente envía `INICIAR-PARTIDA`.
  2. Si hay otro jugador esperando, el servidor crea la partida, genera el tablero y territorios iniciales, y notifica `+Ok. Empieza la partida` a ambos.
  3. Si no hay nadie esperando, responde `+Ok. Esperando otro jugador` y el cliente queda a la espera.
- **Flujo alternativo:** el cliente envía `SALIR` mientras espera → se elimina de la cola de espera.
- **Postcondición:** partida activa con dos jugadores, turno del jugador 1.

### UC-04 Consultar estado del tablero
- **Actor:** Jugador (dentro de una partida)
- **Flujo principal:**
  1. Cliente envía `ESTADO`.
  2. Servidor devuelve los 25 territorios: propietario de cada uno y unidades solo de los territorios propios.
- **Flujo alternativo:** cliente no está en partida → `-Err.`.

### UC-05 Conquistar territorio neutral
- **Actor:** Jugador (en su turno)
- **Precondiciones:** origen y destino existen; destino es neutral; destino adyacente a origen; origen pertenece al jugador con unidades suficientes; es su turno.
- **Flujo principal:**
  1. Cliente envía `CONQUISTAR [origen] [destino] [unidades]`.
  2. Servidor valida todas las precondiciones.
  3. Servidor traslada las unidades indicadas, asigna el destino al jugador y descuenta esas unidades del origen.
  4. Servidor responde `+Ok. Territorio [destino] conquistado.` y pasa el turno al rival.
- **Flujo alternativo:** falla alguna validación → `-Err. <motivo>`, el turno no cambia.

### UC-06 Atacar territorio enemigo
- **Actor:** Jugador (en su turno)
- **Precondiciones:** origen y destino existen y son adyacentes; origen pertenece al atacante; destino pertenece al rival; unidades suficientes en origen; es su turno.
- **Flujo principal:**
  1. Cliente envía `ATACAR [origen] [destino] [unidades]`.
  2. Servidor compara unidades atacantes vs. defensoras.
  3. **Ataque con éxito** (atacantes > defensoras): el destino pasa al atacante con las unidades empleadas en el ataque; el defensor pierde el territorio y sus unidades. Respuesta: `+Ok. Has conquistado el territorio [destino].`
  4. **Ataque fallido** (atacantes ≤ defensoras): el atacante pierde las unidades empleadas; el defensor conserva el territorio con sus unidades. Respuesta: `+Ok. Has perdido el ataque contra [destino].`
  5. Se notifica el resultado a ambos jugadores y se pasa el turno al rival.
- **Flujo alternativo:** falla alguna validación → `-Err. <motivo>`.

### UC-07 Reforzar territorio propio
- **Actor:** Jugador (en su turno)
- **Precondiciones:** origen y destino pertenecen al jugador; son adyacentes; origen tiene unidades suficientes.
- **Flujo principal:**
  1. Cliente envía `REFORZAR [origen] [destino] [unidades]`.
  2. Servidor valida y traslada unidades de origen a destino.
  3. Servidor responde `+Ok. Territorio reforzado.` y pasa el turno.
- **Flujo alternativo:** falla alguna validación → `-Err. <motivo>`.

### UC-08 Pasar turno
- **Actor:** Jugador (en su turno)
- **Flujo principal:**
  1. Cliente envía `PASAR`.
  2. Servidor finaliza el turno sin cambios en el tablero y pasa el turno al rival.

### UC-09 Finalizar partida
- **Actor:** Servidor (disparado automáticamente)
- **Disparadores:** se alcanza el turno máximo (10) o un jugador pierde todos sus territorios.
- **Flujo principal:**
  1. El servidor cuenta los territorios de cada jugador.
  2. Determina el ganador (más territorios; en empate, más unidades totales).
  3. Notifica el resultado a ambos jugadores y cierra la partida.

### UC-10 Salir del servicio
- **Actor:** Jugador
- **Flujo principal:**
  1. Cliente envía `SALIR`.
  2. El servidor lo elimina de los clientes conectados.
  3. Si estaba en partida, finaliza la partida y avisa al rival: `+Ok. Tu oponente ha terminado la partida`.
  4. Si estaba en cola de espera, lo elimina de dicha cola.

### UC-11 Manejo de mensajes inválidos
- **Actor:** Servidor
- **Flujo principal:**
  1. Llega un mensaje que no coincide con ningún comando del protocolo, o con una estructura incorrecta.
  2. El servidor responde `-Err. <motivo>` sin alterar el estado.

## 7. Reglas de validación por comando (resumen)

- **CONQUISTAR:** origen y destino existen · destino neutral · adyacencia · origen del jugador · unidades suficientes · jugador en partida · es su turno.
- **ATACAR:** origen y destino existen · origen del atacante · destino del rival · adyacencia · unidades suficientes · jugador en partida · es su turno.
- **REFORZAR:** ambos territorios del jugador · adyacencia · unidades suficientes en origen.
- **General:** no se puede jugar sin estar validado ni fuera de una partida; no se puede actuar fuera de turno.
