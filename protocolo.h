/*
 * protocolo.h
 * -----------
 * Constantes compartidas por el cliente y el servidor del juego
 * "Dominio Territorial" (Práctica 2 de Redes).
 *
 * Al tenerlas en un fichero común nos aseguramos de que los dos programas
 * usan exactamente el mismo puerto y el mismo tamaño de mensaje.
 */

#ifndef PROTOCOLO_H
#define PROTOCOLO_H

/* Puerto en el que el servidor acepta conexiones (lo fija el enunciado). */
#define PUERTO 2026

/*
 * Tamaño FIJO de cada mensaje que viaja por el socket.
 *
 * Igual que en los ejemplos de clase (send(sd, buffer, sizeof(buffer), 0)),
 * cada mensaje se envía siempre como un bloque de MSG_SIZE bytes: el texto
 * va al principio y el resto se rellena con '\0'.
 *
 * ¿Por qué? TCP es un FLUJO de bytes: no conserva los límites entre
 * mensajes. Si todos los mensajes miden lo mismo, el receptor sabe que cada
 * MSG_SIZE bytes empieza un mensaje nuevo.
 */
#define MSG_SIZE 250

/*
 * Longitud máxima del texto útil de un mensaje (lo fija el enunciado:
 * "tamaño máximo de envío una cadena de longitud 200 caracteres").
 * Los textos más largos (por ejemplo, el tablero) se trocean en varios
 * mensajes de como mucho 200 caracteres.
 */
#define MAX_TEXTO 200

#endif
