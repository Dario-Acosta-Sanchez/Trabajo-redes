/*
 * juego.h
 * -------
 * Lógica del juego "Dominio Territorial": tablero, reglas y recuento.
 *
 * Este módulo NO sabe nada de sockets. Solo modifica el tablero que está en
 * memoria y devuelve los textos de respuesta ("+Ok. ..." / "-Err. ...").
 * El servidor es quien decide a qué cliente mandar cada texto.
 * Separar así la lógica del juego de la comunicación hace el código más
 * fácil de entender y de probar.
 */

#ifndef JUEGO_H
#define JUEGO_H

#include "protocolo.h"

/* Dimensiones del tablero: 5 filas (A-E) x 5 columnas (1-5) = 25 territorios */
#define FILAS    5
#define COLUMNAS 5

/* Unidades con las que empieza cada jugador en su territorio inicial */
#define UNIDADES_INICIALES 30

/*
 * Número máximo de turnos de la partida.
 * Un turno = la acción de UN jugador (conquistar, atacar, reforzar o pasar),
 * así que con 10 turnos cada jugador realiza 5 acciones.
 */
#define MAX_TURNOS 10

/* Posibles propietarios de un territorio */
#define NEUTRAL  0
#define JUGADOR1 1
#define JUGADOR2 2

/* Motivos por los que puede terminar una partida */
#define FIN_NO              0   /* la partida sigue                        */
#define FIN_TURNOS          1   /* se han jugado los MAX_TURNOS turnos     */
#define FIN_SIN_TERRITORIOS 2   /* un jugador se ha quedado sin territorios */

/* Tamaño del buffer donde se dibuja el tablero (12 líneas de ~46 caracteres) */
#define TAM_TABLERO 1024

/* Una casilla del tablero */
typedef struct {
    int propietario;   /* NEUTRAL, JUGADOR1 o JUGADOR2 */
    int unidades;      /* unidades que hay en el territorio */
} Territorio;

/* Una partida entre dos jugadores */
typedef struct {
    int activa;                          /* 1 si la partida está en curso   */
    int sd[3];                           /* sockets de los jugadores: sd[1] = J1,
                                            sd[2] = J2 (sd[0] no se usa, así el
                                            índice coincide con el nº de jugador) */
    Territorio tablero[FILAS][COLUMNAS]; /* el mapa                          */
    int turno;                           /* jugador al que le toca: 1 o 2   */
    int turnosJugados;                   /* acciones válidas realizadas     */
} Partida;

/* Prepara una partida nueva: tablero neutral y un territorio aleatorio por jugador */
void inicializarPartida(Partida *p, int sdJ1, int sdJ2);

/* Devuelve el número del jugador contrario (1 -> 2, 2 -> 1) */
int oponente(int jugador);

/* Convierte un texto como "B3" en fila/columna. Devuelve 1 si el territorio existe */
int parsearTerritorio(const char *texto, int *fila, int *columna);

/* Devuelve 1 si dos casillas son adyacentes (arriba, abajo, izquierda o derecha) */
int sonAdyacentes(int f1, int c1, int f2, int c2);

/*
 * Acciones de juego. Todas siguen el mismo esquema:
 *   - Comprueban las reglas. Si algo falla, escriben "-Err. ..." en
 *     'respuesta' y devuelven 0 (el turno NO se consume).
 *   - Si la acción es válida, modifican el tablero, escriben "+Ok. ..." en
 *     'respuesta', el aviso para el rival en 'aviso' y devuelven 1.
 * Los buffers 'respuesta' y 'aviso' deben tener al menos MSG_SIZE bytes.
 */
int accionConquistar(Partida *p, int jugador, const char *origen, const char *destino,
                     const char *unidades, char *respuesta, char *aviso);
int accionAtacar(Partida *p, int jugador, const char *origen, const char *destino,
                 const char *unidades, char *respuesta, char *aviso);
int accionReforzar(Partida *p, int jugador, const char *origen, const char *destino,
                   const char *unidades, char *respuesta, char *aviso);

/* Cuenta los territorios y las unidades totales de un jugador */
void contarJugador(const Partida *p, int jugador, int *territorios, int *unidades);

/* Devuelve FIN_NO, FIN_TURNOS o FIN_SIN_TERRITORIOS */
int comprobarFinPartida(const Partida *p);

/* Devuelve JUGADOR1, JUGADOR2 o NEUTRAL (empate) según el recuento final */
int calcularGanador(const Partida *p);

/* Escribe en 'texto' el mensaje con el territorio inicial del jugador */
void describirInicio(const Partida *p, int jugador, char *texto);

/*
 * Dibuja el tablero en 'salida' (TAM_TABLERO bytes) visto por 'jugador':
 * de sus territorios se ven las unidades; de los del rival, solo el dueño.
 * Si 'mostrarTodo' es 1 se ven todas las unidades (tablero final).
 */
void generarTablero(const Partida *p, int jugador, int mostrarTodo, char *salida);

#endif
