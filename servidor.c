/*
 * servidor.c
 * ----------
 * Servidor del juego "Dominio Territorial" (Práctica 2 de Redes, UCO).
 *
 * Está construido sobre el esqueleto de ejemploServidorChat.c:
 *   - Socket TCP (SOCK_STREAM) escuchando en el puerto 2026.
 *   - UN SOLO proceso atiende a todos los clientes gracias a select():
 *     el servidor "duerme" hasta que ocurre algo en alguno de los
 *     descriptores que vigila:
 *         · el socket de escucha  -> llega una conexión nueva (accept)
 *         · el descriptor 0       -> el administrador escribe en el teclado
 *         · el socket de un cliente -> ese cliente ha enviado un mensaje
 *
 * Además del esqueleto de red, el servidor:
 *   - Guarda el estado de cada cliente (conectado, validado, esperando, jugando)
 *     para que cada comando solo tenga efecto en el contexto adecuado.
 *   - Valida y registra usuarios (usuarios.c).
 *   - Empareja jugadores y gestiona hasta MAX_PARTIDAS partidas (juego.c).
 *   - Responde SIEMPRE con "+Ok. ..." o "-Err. ...".
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>

#include "protocolo.h"
#include "juego.h"
#include "usuarios.h"

#define MAX_CLIENTES 10    /* máximo de clientes conectados a la vez (enunciado) */
#define MAX_PARTIDAS 4     /* máximo de partidas simultáneas (enunciado)        */

/*
 * Estados por los que pasa un cliente. El servidor consulta el estado antes
 * de ejecutar cada comando (por ejemplo, no se puede INICIAR-PARTIDA sin
 * estar validado, ni ATACAR sin estar jugando).
 *
 *   CONECTADO --USUARIO ok--> USUARIO_OK --PASSWORD ok--> VALIDADO
 *   VALIDADO --INICIAR-PARTIDA--> ESPERANDO --llega rival--> JUGANDO
 *   JUGANDO --fin de partida / rival sale--> VALIDADO
 */
#define ESTADO_CONECTADO   0   /* conectado pero sin identificar               */
#define ESTADO_USUARIO_OK  1   /* ha dado un USUARIO correcto, falta PASSWORD   */
#define ESTADO_VALIDADO    2   /* identificado: puede pedir partida             */
#define ESTADO_ESPERANDO   3   /* ha pedido partida y espera a otro jugador     */
#define ESTADO_JUGANDO     4   /* está dentro de una partida                    */

/* Información que el servidor guarda de cada cliente conectado */
typedef struct {
    int sd;                        /* socket por el que hablamos con él       */
    int estado;                    /* uno de los ESTADO_*                     */
    char usuario[MAX_NOMBRE];      /* nombre de usuario ("" si no lo ha dado) */
    int partida;                   /* índice en partidas[] o -1               */
    int jugador;                   /* JUGADOR1 o JUGADOR2 dentro de la partida */
} Cliente;

/*
 * Estado global del servidor. Son variables globales (static: solo visibles
 * en este fichero) para no tener que pasar todos estos datos como parámetro
 * a cada función.
 */
static Cliente clientes[MAX_CLIENTES];     /* clientes conectados              */
static int numClientes = 0;
static Partida partidas[MAX_PARTIDAS];     /* huecos para partidas             */
static int colaEspera[MAX_CLIENTES];       /* sockets esperando rival (FIFO)   */
static int numEspera = 0;
static fd_set readfds;                     /* descriptores que vigila select() */

/*
 * Bandera que activa el manejador de Ctrl+C. Es 'volatile sig_atomic_t'
 * porque se modifica desde un manejador de señal: así el compilador no la
 * optimiza y su escritura es atómica.
 */
static volatile sig_atomic_t finServidor = 0;

void manejador(int signum);
void salirCliente(int socket);


/* ========================================================================
 *  Envío de mensajes
 * ======================================================================== */

/*
 * Envía un mensaje a un cliente como un bloque fijo de MSG_SIZE bytes
 * (igual que el ejemplo: send(sd, buffer, sizeof(buffer), 0)).
 * send() puede enviar menos bytes de los pedidos, así que repetimos hasta
 * haber enviado el bloque completo.
 */
static void enviarMensaje(int sd, const char *texto)
{
    char buffer[MSG_SIZE];
    int enviados = 0, n;

    bzero(buffer, sizeof(buffer));
    strncpy(buffer, texto, MAX_TEXTO);   /* como mucho 200 caracteres de texto */

    while (enviados < MSG_SIZE) {
        n = send(sd, buffer + enviados, MSG_SIZE - enviados, 0);
        if (n <= 0) {
            /* El cliente ya no está (se cerró su socket). No hacemos nada más:
               select() nos avisará y lo daremos de baja en salirCliente(). */
            return;
        }
        enviados += n;
    }
}

/*
 * Envía un texto de tamaño arbitrario (por ejemplo el tablero, que ocupa
 * más de 200 caracteres) partiéndolo en varios mensajes de como mucho
 * MAX_TEXTO caracteres. Se corta siempre al final de una línea para que el
 * cliente, que simplemente imprime lo que recibe, lo muestre bien.
 */
static void enviarTextoLargo(int sd, const char *texto)
{
    char trozo[MSG_SIZE];
    int longitud = 0, tamLinea;
    const char *linea = texto, *finLinea;

    bzero(trozo, sizeof(trozo));
    while (*linea != '\0') {
        /* Tamaño de la línea actual, incluyendo su '\n' */
        finLinea = strchr(linea, '\n');
        tamLinea = (finLinea != NULL) ? (int)(finLinea - linea + 1) : (int) strlen(linea);
        if (tamLinea > MAX_TEXTO)
            tamLinea = MAX_TEXTO;            /* línea enorme: se corta a la fuerza */

        /* Si la línea ya no cabe en el trozo actual, enviamos lo acumulado */
        if (longitud + tamLinea > MAX_TEXTO) {
            enviarMensaje(sd, trozo);
            bzero(trozo, sizeof(trozo));
            longitud = 0;
        }

        memcpy(trozo + longitud, linea, tamLinea);
        longitud += tamLinea;
        linea += tamLinea;
    }

    if (longitud > 0)
        enviarMensaje(sd, trozo);
}


/* ========================================================================
 *  Funciones auxiliares sobre clientes y cola de espera
 * ======================================================================== */

/* Devuelve el cliente asociado a un socket, o NULL si no existe */
static Cliente *buscarCliente(int sd)
{
    int i;

    for (i = 0; i < numClientes; i++)
        if (clientes[i].sd == sd)
            return &clientes[i];
    return NULL;
}

/* Devuelve 1 si ya hay alguien validado con ese nombre (evita entrar dos veces) */
static int usuarioConectado(const char *nombre)
{
    int i;

    for (i = 0; i < numClientes; i++)
        if (clientes[i].estado >= ESTADO_VALIDADO && strcmp(clientes[i].usuario, nombre) == 0)
            return 1;
    return 0;
}

/* Quita un socket de la cola de espera desplazando el resto una posición */
static void quitarDeCola(int sd)
{
    int i, j;

    for (i = 0; i < numEspera; i++) {
        if (colaEspera[i] == sd) {
            for (j = i; j < numEspera - 1; j++)
                colaEspera[j] = colaEspera[j + 1];
            numEspera--;
            return;
        }
    }
}

/* Quita los '\n', '\r' y espacios del final (el cliente puede mandarlos) */
static void limpiarTexto(char *texto)
{
    int n = strlen(texto);

    while (n > 0 && (texto[n - 1] == '\n' || texto[n - 1] == '\r' || texto[n - 1] == ' '))
        texto[--n] = '\0';
}


/* ========================================================================
 *  Gestión de partidas
 * ======================================================================== */

/* Envía al cliente el estado del tablero visto desde su lado (comando ESTADO) */
static void enviarEstado(Cliente *c)
{
    Partida *p = &partidas[c->partida];
    char tablero[TAM_TABLERO], texto[2 * TAM_TABLERO];
    int misTerritorios, misUnidades, rivalTerritorios, rivalUnidades;

    contarJugador(p, c->jugador, &misTerritorios, &misUnidades);
    contarJugador(p, oponente(c->jugador), &rivalTerritorios, &rivalUnidades);
    generarTablero(p, c->jugador, 0, tablero);   /* 0: unidades del rival ocultas */

    snprintf(texto, sizeof(texto),
             "+Ok. Estado de la partida. Eres J%d. Turno %d de %d. %s\n"
             "%s"
             "Tus territorios: %d (%d unidades) | Territorios del rival: %d\n",
             c->jugador, p->turnosJugados + 1, MAX_TURNOS,
             (p->turno == c->jugador) ? "Te toca jugar." : "Le toca a tu oponente.",
             tablero, misTerritorios, misUnidades, rivalTerritorios);

    /* El texto completo pasa de 200 caracteres: se manda troceado */
    enviarTextoLargo(c->sd, texto);
}

/* Avisa a cada jugador de si le toca o no */
static void avisarTurno(Partida *p)
{
    char texto[MSG_SIZE];

    snprintf(texto, sizeof(texto), "+Ok. Es tu turno (turno %d de %d).",
             p->turnosJugados + 1, MAX_TURNOS);
    enviarMensaje(p->sd[p->turno], texto);

    snprintf(texto, sizeof(texto),
             "+Ok. Turno del oponente (turno %d de %d). Espera a que realice su acción.",
             p->turnosJugados + 1, MAX_TURNOS);
    enviarMensaje(p->sd[oponente(p->turno)], texto);
}

/* Pone en marcha la partida 'indice' entre los clientes de los sockets sd1 (J1) y sd2 (J2) */
static void iniciarPartida(int indice, int sd1, int sd2)
{
    Partida *p = &partidas[indice];
    Cliente *jugadores[3];
    char texto[MSG_SIZE];
    int j;

    inicializarPartida(p, sd1, sd2);   /* tablero neutral + territorios iniciales */

    jugadores[JUGADOR1] = buscarCliente(sd1);
    jugadores[JUGADOR2] = buscarCliente(sd2);

    printf("[Partida %d] Empieza: %s (J1) contra %s (J2)\n",
           indice, jugadores[JUGADOR1]->usuario, jugadores[JUGADOR2]->usuario);

    for (j = JUGADOR1; j <= JUGADOR2; j++) {
        jugadores[j]->estado = ESTADO_JUGANDO;
        jugadores[j]->partida = indice;
        jugadores[j]->jugador = j;

        enviarMensaje(jugadores[j]->sd, "+Ok. Empieza la partida");

        snprintf(texto, sizeof(texto), "+Ok. Juegas contra %s. Eres el Jugador %d (J%d).",
                 jugadores[oponente(j)]->usuario, j, j);
        enviarMensaje(jugadores[j]->sd, texto);

        describirInicio(p, j, texto);
        enviarMensaje(jugadores[j]->sd, texto);

        enviarEstado(jugadores[j]);
    }

    avisarTurno(p);
}

/*
 * Mientras haya al menos 2 clientes esperando y quede algún hueco libre de
 * partida, se emparejan los dos primeros de la cola (orden de llegada).
 * Se llama al pedir partida y cada vez que se libera una partida.
 */
static void emparejarJugadores(void)
{
    int i, libre, sd1, sd2;

    while (numEspera >= 2) {
        libre = -1;
        for (i = 0; i < MAX_PARTIDAS && libre == -1; i++)
            if (!partidas[i].activa)
                libre = i;

        if (libre == -1)
            return;   /* las 4 partidas están ocupadas: siguen esperando */

        sd1 = colaEspera[0];
        sd2 = colaEspera[1];
        quitarDeCola(sd1);
        quitarDeCola(sd2);
        iniciarPartida(libre, sd1, sd2);
    }
}

/* Termina la partida: recuento, ganador, tablero final y vuelta al estado VALIDADO */
static void finalizarPartida(int indice, int motivo)
{
    Partida *p = &partidas[indice];
    Cliente *c;
    char texto[MSG_SIZE], tablero[TAM_TABLERO], textoTablero[2 * TAM_TABLERO];
    int t1, u1, t2, u2, ganador, j;

    contarJugador(p, JUGADOR1, &t1, &u1);
    contarJugador(p, JUGADOR2, &t2, &u2);
    ganador = calcularGanador(p);

    printf("[Partida %d] Termina. J1: %d territorios (%d u.) | J2: %d territorios (%d u.)\n",
           indice, t1, u1, t2, u2);

    for (j = JUGADOR1; j <= JUGADOR2; j++) {
        c = buscarCliente(p->sd[j]);
        if (c == NULL)
            continue;

        if (motivo == FIN_TURNOS)
            snprintf(texto, sizeof(texto),
                     "+Ok. Fin de la partida: se han jugado los %d turnos.", MAX_TURNOS);
        else
            snprintf(texto, sizeof(texto),
                     "+Ok. Fin de la partida: J%d ha perdido todos sus territorios.",
                     (t1 == 0) ? JUGADOR1 : JUGADOR2);
        enviarMensaje(c->sd, texto);

        snprintf(texto, sizeof(texto),
                 "+Ok. Recuento: J1 %d territorios (%d unidades) | J2 %d territorios (%d unidades)",
                 t1, u1, t2, u2);
        enviarMensaje(c->sd, texto);

        if (ganador == NEUTRAL)
            enviarMensaje(c->sd, "+Ok. La partida ha terminado en empate.");
        else if (ganador == j)
            enviarMensaje(c->sd, "+Ok. Has ganado la partida.");
        else
            enviarMensaje(c->sd, "+Ok. Has perdido la partida.");

        /* Tablero final con TODAS las unidades a la vista (mostrarTodo = 1) */
        generarTablero(p, j, 1, tablero);
        snprintf(textoTablero, sizeof(textoTablero), "+Ok. Tablero final:\n%s", tablero);
        enviarTextoLargo(c->sd, textoTablero);

        /* El cliente sigue conectado y validado: puede jugar otra partida */
        c->estado = ESTADO_VALIDADO;
        c->partida = -1;
        c->jugador = 0;
        enviarMensaje(c->sd, "+Ok. Puedes jugar otra partida con INICIAR-PARTIDA o salir con SALIR");
    }

    p->activa = 0;          /* el hueco queda libre ...                         */
    emparejarJugadores();   /* ... y puede usarlo alguien que estuviera esperando */
}

/* Se llama tras cada acción VÁLIDA: cuenta el turno, mira si acaba y cambia de jugador */
static void finalizarTurno(int indice)
{
    Partida *p = &partidas[indice];
    int motivo;

    p->turnosJugados++;

    motivo = comprobarFinPartida(p);
    if (motivo != FIN_NO) {
        finalizarPartida(indice, motivo);
        return;
    }

    p->turno = oponente(p->turno);
    avisarTurno(p);
}


/* ========================================================================
 *  Comandos del protocolo
 *  (n = número de palabras que ha enviado el cliente, comando incluido)
 * ======================================================================== */

/* USUARIO usuario */
static void comandoUsuario(Cliente *c, int n, const char *nombre)
{
    if (c->estado >= ESTADO_VALIDADO) {
        enviarMensaje(c->sd, "-Err. Ya estás validado");
        return;
    }
    if (n != 2) {
        enviarMensaje(c->sd, "-Err. Formato incorrecto. Uso: USUARIO usuario");
        return;
    }

    if (!existeUsuario(nombre)) {
        c->estado = ESTADO_CONECTADO;   /* hay que volver a empezar por USUARIO */
        c->usuario[0] = '\0';
        enviarMensaje(c->sd, "-Err. Usuario incorrecto");
        return;
    }
    if (usuarioConectado(nombre)) {
        c->estado = ESTADO_CONECTADO;
        c->usuario[0] = '\0';
        enviarMensaje(c->sd, "-Err. Usuario incorrecto: ya hay una sesión abierta con ese usuario");
        return;
    }

    /* Guardamos el nombre para comprobar después su password */
    strcpy(c->usuario, nombre);
    c->estado = ESTADO_USUARIO_OK;
    enviarMensaje(c->sd, "+Ok. Usuario correcto");
}

/* PASSWORD password */
static void comandoPassword(Cliente *c, int n, const char *password)
{
    if (c->estado >= ESTADO_VALIDADO) {
        enviarMensaje(c->sd, "-Err. Ya estás validado");
        return;
    }
    if (c->estado != ESTADO_USUARIO_OK) {
        enviarMensaje(c->sd, "-Err. Primero debes indicar el usuario con USUARIO usuario");
        return;
    }
    if (n != 2) {
        enviarMensaje(c->sd, "-Err. Formato incorrecto. Uso: PASSWORD password");
        return;
    }

    if (!comprobarPassword(c->usuario, password)) {
        /* Seguimos en USUARIO_OK: puede volver a intentar la password */
        enviarMensaje(c->sd, "-Err. Error en la validación");
        return;
    }
    /* Segunda comprobación por si otro cliente entró con el mismo usuario entre medias */
    if (usuarioConectado(c->usuario)) {
        enviarMensaje(c->sd, "-Err. Error en la validación: ese usuario ya tiene una sesión abierta");
        return;
    }

    c->estado = ESTADO_VALIDADO;
    printf("Socket %d validado como %s\n", c->sd, c->usuario);
    enviarMensaje(c->sd, "+Ok. Usuario validado");
}

/* REGISTRO usuario password */
static void comandoRegistro(Cliente *c, int n, const char *nombre, const char *password)
{
    if (c->estado >= ESTADO_VALIDADO) {
        enviarMensaje(c->sd, "-Err. Ya estás validado, no puedes registrar otro usuario");
        return;
    }
    if (n != 3) {
        enviarMensaje(c->sd, "-Err. Formato incorrecto. Uso: REGISTRO usuario password");
        return;
    }

    switch (registrarUsuario(nombre, password)) {
        case REG_OK:
            printf("Nuevo usuario registrado: %s\n", nombre);
            enviarMensaje(c->sd, "+Ok. Usuario registrado. Ahora valídate con USUARIO y PASSWORD");
            break;
        case REG_EXISTE:
            enviarMensaje(c->sd, "-Err. Ya existe un usuario con ese nombre");
            break;
        case REG_INVALIDO:
            enviarMensaje(c->sd, "-Err. Usuario o password demasiado largos (máximo 49 caracteres)");
            break;
        default:
            enviarMensaje(c->sd, "-Err. No se ha podido registrar el usuario");
            break;
    }
}

/* INICIAR-PARTIDA */
static void comandoIniciarPartida(Cliente *c, int n)
{
    int sd = c->sd;

    if (n != 1) {
        enviarMensaje(sd, "-Err. Formato incorrecto. Uso: INICIAR-PARTIDA");
        return;
    }

    switch (c->estado) {
        case ESTADO_CONECTADO:
        case ESTADO_USUARIO_OK:
            enviarMensaje(sd, "-Err. Debes validarte (USUARIO y PASSWORD) antes de iniciar una partida");
            return;
        case ESTADO_ESPERANDO:
            enviarMensaje(sd, "-Err. Ya estás esperando a otro jugador");
            return;
        case ESTADO_JUGANDO:
            enviarMensaje(sd, "-Err. Ya estás jugando una partida");
            return;
    }

    /* Está VALIDADO: lo metemos al final de la cola e intentamos emparejar */
    colaEspera[numEspera++] = sd;
    c->estado = ESTADO_ESPERANDO;
    printf("%s pide partida (en espera: %d)\n", c->usuario, numEspera);

    emparejarJugadores();

    /* Si no se ha formado pareja, sigue esperando: se lo decimos */
    if (c->estado == ESTADO_ESPERANDO) {
        enviarMensaje(sd, "+Ok. Esperando otro jugador");
        if (numEspera >= 2)
            enviarMensaje(sd, "+Ok. Las 4 partidas están ocupadas: la tuya empezará cuando quede una libre");
    }
}

/* ESTADO, CONQUISTAR, ATACAR, REFORZAR y PASAR */
static void comandoJuego(Cliente *c, const char *comando, int n,
                         const char *origen, const char *destino, const char *unidades)
{
    Partida *p;
    char respuesta[MSG_SIZE], aviso[MSG_SIZE];
    int valida = 0;
    int sinArgumentos = (strcmp(comando, "ESTADO") == 0 || strcmp(comando, "PASAR") == 0);

    /* 1) El jugador tiene que estar dentro de una partida */
    if (c->estado != ESTADO_JUGANDO) {
        if (c->estado == ESTADO_ESPERANDO)
            enviarMensaje(c->sd, "-Err. Tu partida aún no ha empezado: esperando otro jugador");
        else
            enviarMensaje(c->sd, "-Err. No estás dentro de ninguna partida");
        return;
    }

    /* 2) Sintaxis: número correcto de argumentos */
    if (sinArgumentos && n != 1) {
        snprintf(respuesta, sizeof(respuesta), "-Err. Formato incorrecto. Uso: %s", comando);
        enviarMensaje(c->sd, respuesta);
        return;
    }
    if (!sinArgumentos && n != 4) {
        snprintf(respuesta, sizeof(respuesta),
                 "-Err. Formato incorrecto. Uso: %s origen destino unidades", comando);
        enviarMensaje(c->sd, respuesta);
        return;
    }

    p = &partidas[c->partida];

    /* ESTADO se puede pedir en cualquier momento, sea o no tu turno */
    if (strcmp(comando, "ESTADO") == 0) {
        enviarEstado(c);
        return;
    }

    /* 3) Para el resto de acciones tiene que ser su turno */
    if (p->turno != c->jugador) {
        enviarMensaje(c->sd, "-Err. No es tu turno");
        return;
    }

    /* 4) Reglas del juego (juego.c) */
    if (strcmp(comando, "PASAR") == 0) {
        strcpy(respuesta, "+Ok. Has pasado el turno.");
        strcpy(aviso, "+Ok. Tu oponente ha pasado el turno.");
        valida = 1;
    } else if (strcmp(comando, "CONQUISTAR") == 0) {
        valida = accionConquistar(p, c->jugador, origen, destino, unidades, respuesta, aviso);
    } else if (strcmp(comando, "ATACAR") == 0) {
        valida = accionAtacar(p, c->jugador, origen, destino, unidades, respuesta, aviso);
    } else {
        valida = accionReforzar(p, c->jugador, origen, destino, unidades, respuesta, aviso);
    }

    enviarMensaje(c->sd, respuesta);

    /* Si la acción era válida: se avisa al rival y el turno pasa al otro jugador.
       Si no, el jugador recibe el -Err y puede volver a intentarlo. */
    if (valida) {
        printf("[Partida %d] %s (J%d): %s\n", c->partida, c->usuario, c->jugador, respuesta);
        enviarMensaje(p->sd[oponente(c->jugador)], aviso);
        finalizarTurno(c->partida);
    }
}

/*
 * Analiza un mensaje recibido de un cliente y llama a la función del comando.
 * Primero se trocea en palabras con sscanf: n es el número de palabras leídas.
 */
static void procesarMensaje(int sd, char *buffer)
{
    Cliente *c = buscarCliente(sd);
    /* Cada palabra cabe seguro en MSG_SIZE porque el mensaje entero mide como mucho eso */
    char comando[MSG_SIZE] = "", arg1[MSG_SIZE] = "", arg2[MSG_SIZE] = "",
         arg3[MSG_SIZE] = "", sobra[MSG_SIZE] = "";
    int n;

    if (c == NULL)
        return;

    limpiarTexto(buffer);

    if (strlen(buffer) > MAX_TEXTO) {
        enviarMensaje(sd, "-Err. Mensaje demasiado largo (máximo 200 caracteres)");
        return;
    }

    /* Se leen hasta 5 palabras; si llega una quinta es que sobran argumentos */
    n = sscanf(buffer, "%s %s %s %s %s", comando, arg1, arg2, arg3, sobra);
    if (n <= 0)
        return;   /* mensaje vacío: se ignora */

    printf("Socket %d (%s): %s\n", sd, c->usuario[0] ? c->usuario : "sin validar", buffer);

    if (strcmp(comando, "USUARIO") == 0)
        comandoUsuario(c, n, arg1);
    else if (strcmp(comando, "PASSWORD") == 0)
        comandoPassword(c, n, arg1);
    else if (strcmp(comando, "REGISTRO") == 0)
        comandoRegistro(c, n, arg1, arg2);
    else if (strcmp(comando, "INICIAR-PARTIDA") == 0)
        comandoIniciarPartida(c, n);
    else if (strcmp(comando, "ESTADO") == 0 || strcmp(comando, "CONQUISTAR") == 0 ||
             strcmp(comando, "ATACAR") == 0 || strcmp(comando, "REFORZAR") == 0 ||
             strcmp(comando, "PASAR") == 0)
        comandoJuego(c, comando, n, arg1, arg2, arg3);
    else if (strcmp(comando, "SALIR") == 0) {
        if (n != 1) {
            enviarMensaje(sd, "-Err. Formato incorrecto. Uso: SALIR");
        } else {
            enviarMensaje(sd, "+Ok. Desconexión procesada");
            salirCliente(sd);
        }
    } else
        enviarMensaje(sd, "-Err. Comando no reconocido");
}


/* ========================================================================
 *  Bajas de clientes y cierre del servidor
 * ======================================================================== */

/*
 * Da de baja a un cliente: se usa cuando envía SALIR y cuando se desconecta
 * sin avisar (Ctrl+C en el cliente -> recv() devuelve 0).
 *   - Si estaba esperando, se quita de la cola.
 *   - Si estaba jugando, se termina la partida y se avisa al rival.
 *   - Se cierra su socket, se quita de readfds y se elimina del vector.
 */
void salirCliente(int socket)
{
    Cliente *c = buscarCliente(socket);
    Cliente *rival;
    Partida *p;
    int j, sdRival, partidaLiberada = 0;

    if (c != NULL) {
        printf("Sale el socket %d (%s)\n", socket, c->usuario[0] ? c->usuario : "sin validar");

        if (c->estado == ESTADO_ESPERANDO) {
            quitarDeCola(socket);
        } else if (c->estado == ESTADO_JUGANDO) {
            p = &partidas[c->partida];
            sdRival = p->sd[oponente(c->jugador)];

            enviarMensaje(sdRival, "+Ok. Tu oponente ha terminado la partida");

            rival = buscarCliente(sdRival);
            if (rival != NULL) {
                rival->estado = ESTADO_VALIDADO;   /* el rival puede pedir otra partida */
                rival->partida = -1;
                rival->jugador = 0;
                enviarMensaje(sdRival, "+Ok. Puedes jugar otra partida con INICIAR-PARTIDA o salir con SALIR");
            }

            p->activa = 0;
            partidaLiberada = 1;
            printf("[Partida %d] Cancelada porque un jugador ha salido\n", c->partida);
        }

        /* Re-estructurar el vector de clientes (igual que en el ejemplo del chat) */
        for (j = c - clientes; j < numClientes - 1; j++)
            clientes[j] = clientes[j + 1];
        numClientes--;
    }

    close(socket);
    FD_CLR(socket, &readfds);

    /* Con la partida libre, quizá puedan empezar dos jugadores que esperaban */
    if (partidaLiberada)
        emparejarJugadores();
}

/* Avisa a todos los clientes, cierra todos los sockets y termina */
static void cerrarServidor(int sd)
{
    int j;

    printf("Cerrando el servidor...\n");
    for (j = 0; j < numClientes; j++) {
        enviarMensaje(clientes[j].sd, "+Ok. Desconexión servidor");
        close(clientes[j].sd);
    }
    close(sd);
    exit(0);
}

/*
 * Manejador de Ctrl+C (SIGINT). Dentro de un manejador de señal solo es
 * seguro hacer cosas muy simples, así que únicamente activamos la bandera:
 * select() se interrumpe (devuelve -1 con errno = EINTR), el bucle principal
 * ve la bandera y hace el cierre ordenado con cerrarServidor().
 */
void manejador(int signum)
{
    (void) signum;
    finServidor = 1;
}


/* ========================================================================
 *  Programa principal
 * ======================================================================== */

int main()
{
    /*----------------------------------------------------
        Descriptor del socket y buffer de datos
    -----------------------------------------------------*/
    int sd, new_sd;
    struct sockaddr_in sockname, from;
    char buffer[MSG_SIZE + 1];    /* +1 para poder poner siempre un '\0' al final */
    socklen_t from_len;
    fd_set auxfds;
    int salida, i, recibidos, on;

    srand(time(NULL));     /* semilla para elegir los territorios iniciales al azar */
    cargarUsuarios();

    /* --------------------------------------------------
        Se abre el socket TCP (SOCK_STREAM)
    ---------------------------------------------------*/
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("No se puede abrir el socket servidor");
        exit(1);
    }

    /*
     * SO_REUSEADDR permite volver a enlazar el puerto nada más cerrar el
     * servidor, sin esperar a que el puerto salga del estado TIME_WAIT de TCP.
     */
    on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* Dirección del servidor: cualquier interfaz local (INADDR_ANY), puerto 2026.
       htons pasa el puerto al orden de bytes de la red (big-endian). */
    bzero(&sockname, sizeof(sockname));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(PUERTO);
    sockname.sin_addr.s_addr = INADDR_ANY;

    if (bind(sd, (struct sockaddr *) &sockname, sizeof(sockname)) == -1) {
        perror("Error en la operación bind");
        exit(1);
    }

    /* listen convierte el socket en pasivo: a partir de aquí recibe conexiones.
       El segundo parámetro es el tamaño de la cola de conexiones pendientes. */
    if (listen(sd, MAX_CLIENTES) == -1) {
        perror("Error en la operación de listen");
        exit(1);
    }

    printf("Servidor de Dominio Territorial escuchando en el puerto %d\n", PUERTO);
    printf("Escribe SALIR (o pulsa Ctrl+C) para cerrar el servidor\n");

    /* Inicializar los conjuntos fd_set: vigilamos el socket de escucha y el teclado */
    FD_ZERO(&readfds);
    FD_ZERO(&auxfds);
    FD_SET(sd, &readfds);
    FD_SET(0, &readfds);

    /* Ninguna partida en curso al arrancar */
    for (i = 0; i < MAX_PARTIDAS; i++)
        partidas[i].activa = 0;

    /* Capturamos la señal SIGINT (Ctrl+C) */
    signal(SIGINT, manejador);

    /*
     * Ignoramos SIGPIPE. Si hacemos send() a un cliente que ya ha cerrado su
     * socket, el sistema mandaría SIGPIPE y mataría al servidor entero. Al
     * ignorarla, send() simplemente devuelve -1 y seguimos funcionando.
     */
    signal(SIGPIPE, SIG_IGN);

    /*-----------------------------------------------------------------------
        Bucle principal: el servidor atiende peticiones hasta que se cierra
    ------------------------------------------------------------------------ */
    while (!finServidor) {

        /*
         * select() MODIFICA el conjunto que recibe (deja marcados solo los
         * descriptores que tienen algo), así que trabajamos sobre una copia
         * (auxfds) y mantenemos intacto readfds.
         * timeout = NULL -> select se bloquea hasta que ocurra algo.
         */
        auxfds = readfds;
        salida = select(FD_SETSIZE, &auxfds, NULL, NULL, NULL);

        if (salida < 0) {
            if (errno != EINTR)        /* EINTR = interrumpido por Ctrl+C */
                perror("Error en select");
            continue;
        }

        /* Recorremos los descriptores para ver cuál(es) tienen actividad */
        for (i = 0; i < FD_SETSIZE && !finServidor; i++) {

            if (!FD_ISSET(i, &auxfds))
                continue;

            if (i == sd) {
                /* ---------- Nueva conexión en el socket de escucha ---------- */
                from_len = sizeof(from);
                new_sd = accept(sd, (struct sockaddr *) &from, &from_len);
                if (new_sd == -1) {
                    perror("Error aceptando peticiones");
                } else if (numClientes < MAX_CLIENTES) {
                    /* Hay hueco: lo añadimos al vector y a los descriptores vigilados */
                    clientes[numClientes].sd = new_sd;
                    clientes[numClientes].estado = ESTADO_CONECTADO;
                    clientes[numClientes].usuario[0] = '\0';
                    clientes[numClientes].partida = -1;
                    clientes[numClientes].jugador = 0;
                    numClientes++;
                    FD_SET(new_sd, &readfds);

                    printf("Nuevo cliente en el socket %d desde %s:%d (%d/%d conectados)\n",
                           new_sd, inet_ntoa(from.sin_addr), ntohs(from.sin_port),
                           numClientes, MAX_CLIENTES);
                    enviarMensaje(new_sd, "+Ok. Usuario conectado");
                } else {
                    /* Ya hay 10 clientes: se le avisa y se cierra la conexión */
                    enviarMensaje(new_sd, "-Err. Demasiados clientes conectados");
                    close(new_sd);
                }

            } else if (i == 0) {
                /* ---------- El administrador ha escrito en el teclado ---------- */
                bzero(buffer, sizeof(buffer));
                if (fgets(buffer, MSG_SIZE, stdin) == NULL) {
                    /* Sin teclado (fin de fichero): dejamos de vigilar la entrada estándar */
                    FD_CLR(0, &readfds);
                    continue;
                }
                limpiarTexto(buffer);
                if (strcmp(buffer, "SALIR") == 0)
                    finServidor = 1;
                else
                    printf("Comando no reconocido. Escribe SALIR para cerrar el servidor\n");

            } else {
                /* ---------- Mensaje de un cliente ya conectado ---------- */
                bzero(buffer, sizeof(buffer));
                recibidos = recv(i, buffer, MSG_SIZE, 0);

                if (recibidos > 0) {
                    buffer[recibidos] = '\0';
                    procesarMensaje(i, buffer);
                } else {
                    /* recv devuelve 0 si el cliente cerró la conexión (p. ej. Ctrl+C)
                       y -1 si hubo un error: en ambos casos lo damos de baja */
                    printf("El socket %d se ha desconectado sin enviar SALIR\n", i);
                    salirCliente(i);
                }
            }
        }
    }

    cerrarServidor(sd);
    return 0;
}
