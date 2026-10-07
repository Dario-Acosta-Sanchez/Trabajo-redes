/*
 * juego.c
 * -------
 * Implementación de las reglas del juego "Dominio Territorial".
 *
 * Recordatorio de las reglas (enunciado, apartado 2.2):
 *   - Tablero 5x5. Cada territorio es Neutral, de J1 o de J2.
 *   - Cada jugador empieza con 30 unidades en un territorio aleatorio.
 *   - En cada turno se hace UNA acción: CONQUISTAR, ATACAR, REFORZAR o PASAR.
 *   - Adyacencia: arriba, abajo, izquierda y derecha (no diagonales).
 *   - La partida acaba al llegar a MAX_TURNOS o cuando un jugador se queda
 *     sin territorios. Gana quien tenga más territorios; si empatan, quien
 *     tenga más unidades.
 *
 * Todos los textos de respuesta se escriben con snprintf limitado a
 * MAX_TEXTO + 1 bytes, así nunca superan los 200 caracteres del protocolo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "juego.h"

/* Tamaño que se pasa a snprintf: 200 caracteres de texto + el '\0' final */
#define TAM_RESPUESTA (MAX_TEXTO + 1)


/* ========================================================================
 *  Funciones auxiliares (static = solo visibles dentro de este fichero)
 * ======================================================================== */

/* Escribe en 'nombre' el identificador de una casilla: (1, 2) -> "B3" */
static void nombreTerritorio(int fila, int columna, char *nombre)
{
    sprintf(nombre, "%c%d", 'A' + fila, columna + 1);
}

/*
 * Convierte el texto de las unidades en un entero.
 * Devuelve 1 solo si TODO el texto es un número entero mayor que 0.
 * strtol deja en 'fin' el primer carácter que no ha podido convertir;
 * si no es el '\0' final, había basura (por ejemplo "10a").
 */
static int parsearUnidades(const char *texto, int *unidades)
{
    char *fin;
    long valor = strtol(texto, &fin, 10);

    if (*texto == '\0' || *fin != '\0' || valor <= 0 || valor > 100000)
        return 0;

    *unidades = (int) valor;
    return 1;
}

/*
 * Comprobaciones comunes a CONQUISTAR, ATACAR y REFORZAR:
 *   - el territorio de origen existe,
 *   - el territorio de destino existe,
 *   - el número de unidades es un entero positivo.
 * Si todo es correcto rellena las coordenadas y las unidades y devuelve 1.
 * Si no, escribe el error en 'respuesta' y devuelve 0.
 */
static int leerMovimiento(const char *origen, const char *destino, const char *unidades,
                          int *fo, int *co, int *fd, int *cd, int *n, char *respuesta)
{
    if (!parsearTerritorio(origen, fo, co)) {
        /* %.10s: como mucho 10 caracteres del texto recibido, por si es muy largo */
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio de origen %.10s no existe (use A1..E5)", origen);
        return 0;
    }
    if (!parsearTerritorio(destino, fd, cd)) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio de destino %.10s no existe (use A1..E5)", destino);
        return 0;
    }
    if (!parsearUnidades(unidades, n)) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El número de unidades debe ser un entero mayor que 0");
        return 0;
    }
    return 1;
}


/* ========================================================================
 *  Funciones públicas (declaradas en juego.h)
 * ======================================================================== */

void inicializarPartida(Partida *p, int sdJ1, int sdJ2)
{
    int f, c;
    int f1, c1, f2, c2;

    /* Todo el tablero empieza neutral y sin unidades */
    for (f = 0; f < FILAS; f++) {
        for (c = 0; c < COLUMNAS; c++) {
            p->tablero[f][c].propietario = NEUTRAL;
            p->tablero[f][c].unidades = 0;
        }
    }

    /* Territorio inicial aleatorio para J1 */
    f1 = rand() % FILAS;
    c1 = rand() % COLUMNAS;

    /* Territorio inicial aleatorio para J2, repitiendo hasta que sea distinto al de J1 */
    do {
        f2 = rand() % FILAS;
        c2 = rand() % COLUMNAS;
    } while (f2 == f1 && c2 == c1);

    p->tablero[f1][c1].propietario = JUGADOR1;
    p->tablero[f1][c1].unidades = UNIDADES_INICIALES;
    p->tablero[f2][c2].propietario = JUGADOR2;
    p->tablero[f2][c2].unidades = UNIDADES_INICIALES;

    p->sd[0] = -1;              /* posición sin uso */
    p->sd[JUGADOR1] = sdJ1;
    p->sd[JUGADOR2] = sdJ2;
    p->turno = JUGADOR1;        /* empieza el jugador que llevaba más tiempo esperando */
    p->turnosJugados = 0;
    p->activa = 1;
}

int oponente(int jugador)
{
    return (jugador == JUGADOR1) ? JUGADOR2 : JUGADOR1;
}

int parsearTerritorio(const char *texto, int *fila, int *columna)
{
    char letra, numero;

    /* Un territorio siempre son exactamente 2 caracteres: letra + número */
    if (strlen(texto) != 2)
        return 0;

    letra = toupper((unsigned char) texto[0]);   /* aceptamos "b3" como "B3" */
    numero = texto[1];

    if (letra < 'A' || letra >= 'A' + FILAS)
        return 0;
    if (numero < '1' || numero >= '1' + COLUMNAS)
        return 0;

    /* Pasamos a índices de la matriz: 'A' -> 0, '1' -> 0 */
    *fila = letra - 'A';
    *columna = numero - '1';
    return 1;
}

int sonAdyacentes(int f1, int c1, int f2, int c2)
{
    /*
     * Dos casillas son adyacentes si están a distancia 1 en horizontal o en
     * vertical (distancia Manhattan = 1). Así las diagonales (distancia 2)
     * y la propia casilla (distancia 0) quedan excluidas.
     */
    return abs(f1 - f2) + abs(c1 - c2) == 1;
}

int accionConquistar(Partida *p, int jugador, const char *origen, const char *destino,
                     const char *unidades, char *respuesta, char *aviso)
{
    int fo, co, fd, cd, n;
    char nomOrigen[3], nomDestino[3];
    Territorio *tOrigen, *tDestino;

    /* 1) Origen y destino existen, unidades es un número válido */
    if (!leerMovimiento(origen, destino, unidades, &fo, &co, &fd, &cd, &n, respuesta))
        return 0;

    tOrigen = &p->tablero[fo][co];
    tDestino = &p->tablero[fd][cd];
    nombreTerritorio(fo, co, nomOrigen);
    nombreTerritorio(fd, cd, nomDestino);

    /* 2) El territorio de origen pertenece al jugador */
    if (tOrigen->propietario != jugador) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio de origen %s no te pertenece", nomOrigen);
        return 0;
    }

    /* 3) El destino está sin conquistar (neutral) */
    if (tDestino->propietario != NEUTRAL) {
        if (tDestino->propietario == jugador)
            snprintf(respuesta, TAM_RESPUESTA,
                     "-Err. El territorio %s ya es tuyo. Usa REFORZAR para mover unidades", nomDestino);
        else
            snprintf(respuesta, TAM_RESPUESTA,
                     "-Err. El territorio %s no es neutral. Usa ATACAR para quitárselo al rival", nomDestino);
        return 0;
    }

    /* 4) Origen y destino son adyacentes */
    if (!sonAdyacentes(fo, co, fd, cd)) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. %s y %s no son adyacentes", nomOrigen, nomDestino);
        return 0;
    }

    /* 5) Hay suficientes unidades en el origen */
    if (n > tOrigen->unidades) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. No tienes suficientes unidades en %s (disponibles: %d)",
                 nomOrigen, tOrigen->unidades);
        return 0;
    }

    /* Todo correcto: se trasladan las unidades y el destino pasa a ser del jugador */
    tOrigen->unidades -= n;
    tDestino->propietario = jugador;
    tDestino->unidades = n;

    snprintf(respuesta, TAM_RESPUESTA, "+Ok. Territorio %s conquistado.", nomDestino);
    snprintf(aviso, TAM_RESPUESTA, "+Ok. Tu oponente ha conquistado el territorio %s.", nomDestino);
    return 1;
}

int accionAtacar(Partida *p, int jugador, const char *origen, const char *destino,
                 const char *unidades, char *respuesta, char *aviso)
{
    int fo, co, fd, cd, n;
    char nomOrigen[3], nomDestino[3];
    Territorio *tOrigen, *tDestino;

    /* 1) Origen y destino existen, unidades es un número válido */
    if (!leerMovimiento(origen, destino, unidades, &fo, &co, &fd, &cd, &n, respuesta))
        return 0;

    tOrigen = &p->tablero[fo][co];
    tDestino = &p->tablero[fd][cd];
    nombreTerritorio(fo, co, nomOrigen);
    nombreTerritorio(fd, cd, nomDestino);

    /* 2) El territorio de origen pertenece al atacante */
    if (tOrigen->propietario != jugador) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio de origen %s no te pertenece", nomOrigen);
        return 0;
    }

    /* 3) El destino pertenece al jugador contrario */
    if (tDestino->propietario == jugador) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. No puedes atacar uno de tus propios territorios (%s)", nomDestino);
        return 0;
    }
    if (tDestino->propietario == NEUTRAL) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio %s es neutral. Usa CONQUISTAR para ocuparlo", nomDestino);
        return 0;
    }

    /* 4) Ambos territorios son adyacentes */
    if (!sonAdyacentes(fo, co, fd, cd)) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. %s y %s no son adyacentes", nomOrigen, nomDestino);
        return 0;
    }

    /* 5) Hay suficientes unidades en el origen */
    if (n > tOrigen->unidades) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. No tienes suficientes unidades en %s (disponibles: %d)",
                 nomOrigen, tOrigen->unidades);
        return 0;
    }

    /* Las unidades que participan en el ataque salen siempre del origen */
    tOrigen->unidades -= n;

    /*
     * Resolución del combate: se comparan atacantes con defensores.
     *   - Atacantes > defensores: el atacante conquista el territorio; los
     *     defensores se pierden y los atacantes ocupan el territorio.
     *   - Atacantes <= defensores: el ataque fracasa y los atacantes se
     *     pierden. En caso de EMPATE gana el defensor (el enunciado solo dice
     *     que se conquista si los atacantes son "superiores").
     */
    if (n > tDestino->unidades) {
        tDestino->propietario = jugador;
        tDestino->unidades = n;

        snprintf(respuesta, TAM_RESPUESTA, "+Ok. Has conquistado el territorio %s.", nomDestino);
        snprintf(aviso, TAM_RESPUESTA,
                 "+Ok. Tu oponente te ha atacado desde %s con %d unidades y ha conquistado tu territorio %s.",
                 nomOrigen, n, nomDestino);
    } else {
        snprintf(respuesta, TAM_RESPUESTA, "+Ok. Has perdido el ataque contra %s.", nomDestino);
        snprintf(aviso, TAM_RESPUESTA,
                 "+Ok. Tu oponente ha atacado %s desde %s con %d unidades, pero has resistido el ataque.",
                 nomDestino, nomOrigen, n);
    }
    return 1;
}

int accionReforzar(Partida *p, int jugador, const char *origen, const char *destino,
                   const char *unidades, char *respuesta, char *aviso)
{
    int fo, co, fd, cd, n;
    char nomOrigen[3], nomDestino[3];
    Territorio *tOrigen, *tDestino;

    /* 1) Origen y destino existen, unidades es un número válido */
    if (!leerMovimiento(origen, destino, unidades, &fo, &co, &fd, &cd, &n, respuesta))
        return 0;

    tOrigen = &p->tablero[fo][co];
    tDestino = &p->tablero[fd][cd];
    nombreTerritorio(fo, co, nomOrigen);
    nombreTerritorio(fd, cd, nomDestino);

    /* 2) Los dos territorios pertenecen al jugador */
    if (tOrigen->propietario != jugador) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. El territorio de origen %s no te pertenece", nomOrigen);
        return 0;
    }
    if (tDestino->propietario != jugador) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. No puedes reforzar %s porque no te pertenece", nomDestino);
        return 0;
    }

    /* 3) Son adyacentes */
    if (!sonAdyacentes(fo, co, fd, cd)) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. %s y %s no son adyacentes", nomOrigen, nomDestino);
        return 0;
    }

    /* 4) Hay suficientes unidades en el origen */
    if (n > tOrigen->unidades) {
        snprintf(respuesta, TAM_RESPUESTA,
                 "-Err. No tienes suficientes unidades en %s (disponibles: %d)",
                 nomOrigen, tOrigen->unidades);
        return 0;
    }

    /* Se trasladan las unidades de un territorio propio a otro */
    tOrigen->unidades -= n;
    tDestino->unidades += n;

    snprintf(respuesta, TAM_RESPUESTA, "+Ok. Territorio reforzado.");
    /* Al rival no le decimos cuántas unidades se han movido (información oculta) */
    snprintf(aviso, TAM_RESPUESTA, "+Ok. Tu oponente ha reforzado sus territorios.");
    return 1;
}

void contarJugador(const Partida *p, int jugador, int *territorios, int *unidades)
{
    int f, c;

    *territorios = 0;
    *unidades = 0;
    for (f = 0; f < FILAS; f++) {
        for (c = 0; c < COLUMNAS; c++) {
            if (p->tablero[f][c].propietario == jugador) {
                (*territorios)++;
                *unidades += p->tablero[f][c].unidades;
            }
        }
    }
}

int comprobarFinPartida(const Partida *p)
{
    int t1, u1, t2, u2;

    contarJugador(p, JUGADOR1, &t1, &u1);
    contarJugador(p, JUGADOR2, &t2, &u2);

    /* Algún jugador ha perdido el control de todos sus territorios */
    if (t1 == 0 || t2 == 0)
        return FIN_SIN_TERRITORIOS;

    /* Se ha alcanzado el número máximo de turnos */
    if (p->turnosJugados >= MAX_TURNOS)
        return FIN_TURNOS;

    return FIN_NO;
}

int calcularGanador(const Partida *p)
{
    int t1, u1, t2, u2;

    contarJugador(p, JUGADOR1, &t1, &u1);
    contarJugador(p, JUGADOR2, &t2, &u2);

    /* Criterio principal: número de territorios */
    if (t1 != t2)
        return (t1 > t2) ? JUGADOR1 : JUGADOR2;

    /* Criterio secundario (desempate): unidades totales */
    if (u1 != u2)
        return (u1 > u2) ? JUGADOR1 : JUGADOR2;

    return NEUTRAL;   /* empate total */
}

void describirInicio(const Partida *p, int jugador, char *texto)
{
    int f, c;
    char nombre[3];

    /* Al empezar, cada jugador tiene exactamente un territorio: lo buscamos */
    for (f = 0; f < FILAS; f++) {
        for (c = 0; c < COLUMNAS; c++) {
            if (p->tablero[f][c].propietario == jugador) {
                nombreTerritorio(f, c, nombre);
                snprintf(texto, TAM_RESPUESTA,
                         "+Ok. Tu territorio inicial es %s con %d unidades.",
                         nombre, p->tablero[f][c].unidades);
                return;
            }
        }
    }
    snprintf(texto, TAM_RESPUESTA, "+Ok. No tienes territorios.");
}

void generarTablero(const Partida *p, int jugador, int mostrarTodo, char *salida)
{
    /*
     * Formato (cada casilla ocupa 7 caracteres + el separador '|'):
     *
     *         1       2       3       4       5
     *     +-------+-------+-------+-------+-------+
     *   A |       |       |       |       |       |
     *     +-------+-------+-------+-------+-------+
     *   B |       |J1(25) | J1(5) |       |       |
     *   ...
     */
    const char *borde = "    +-------+-------+-------+-------+-------+\n";
    char celda[16], linea[128];
    int f, c, largo, izquierda, derecha;
    const Territorio *t;

    salida[0] = '\0';
    strcat(salida, "        1       2       3       4       5\n");
    strcat(salida, borde);

    for (f = 0; f < FILAS; f++) {
        sprintf(linea, "  %c |", 'A' + f);
        strcat(salida, linea);

        for (c = 0; c < COLUMNAS; c++) {
            t = &p->tablero[f][c];

            if (t->propietario == NEUTRAL)
                celda[0] = '\0';                                   /* neutral: vacío */
            else if (t->propietario == jugador || mostrarTodo)
                sprintf(celda, "J%d(%d)", t->propietario, t->unidades); /* se ven las unidades */
            else
                sprintf(celda, "J%d", t->propietario);             /* rival: unidades ocultas */

            /* Centramos el contenido dentro de los 7 caracteres de la casilla */
            largo = strlen(celda);
            izquierda = (7 - largo) / 2;
            derecha = 7 - largo - izquierda;
            sprintf(linea, "%*s%s%*s|", izquierda, "", celda, derecha, "");
            strcat(salida, linea);
        }
        strcat(salida, "\n");
        strcat(salida, borde);
    }
}
