/*
 * cliente.c
 * ---------
 * Cliente del juego "Dominio Territorial" (Práctica 2 de Redes, UCO).
 *
 * Está construido sobre ejemploClienteChat.c. El cliente es "tonto": no
 * conoce las reglas del juego. Solo hace dos cosas a la vez:
 *   - Lo que el usuario escribe por teclado se lo manda al servidor.
 *   - Lo que llega del servidor lo muestra por pantalla.
 *
 * Para poder atender a las dos cosas sin quedarse bloqueado en una de ellas
 * usamos select() sobre el teclado (descriptor 0) y sobre el socket. Esto es
 * necesario porque el servidor puede mandarnos mensajes en cualquier momento
 * (por ejemplo "Empieza la partida" o la jugada del rival) aunque nosotros
 * no hayamos escrito nada.
 *
 * Uso:  ./cliente [ip_o_nombre_del_servidor]     (por defecto 127.0.0.1)
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
#include <arpa/inet.h>

#include "protocolo.h"

static void mostrarAyuda(void)
{
    printf("------------------------------------------------------------\n");
    printf(" DOMINIO TERRITORIAL - comandos disponibles\n");
    printf("   REGISTRO usuario password   USUARIO usuario   PASSWORD password\n");
    printf("   INICIAR-PARTIDA   ESTADO   PASAR   SALIR\n");
    printf("   CONQUISTAR origen destino unidades   (ej: CONQUISTAR B2 B3 10)\n");
    printf("   ATACAR     origen destino unidades   (ej: ATACAR B3 C3 15)\n");
    printf("   REFORZAR   origen destino unidades   (ej: REFORZAR B2 B3 2)\n");
    printf("------------------------------------------------------------\n");
}

int main(int argc, char *argv[])
{
    /*----------------------------------------------------
        Descriptor del socket y buffers de datos
    -----------------------------------------------------*/
    int sd;
    struct sockaddr_in sockname;
    struct hostent *host;
    char buffer[MSG_SIZE + 1];       /* mensaje recibido (+1 para el '\0')        */
    char linea[MAX_TEXTO + 2];       /* línea del teclado: 200 caracteres + '\n' + '\0' */
    socklen_t len_sockname;
    fd_set readfds, auxfds;
    int salida, recibidos, largo, c;
    int fin = 0;
    const char *servidor = "127.0.0.1";

    /* El servidor se puede indicar como argumento (útil para jugar desde otro equipo) */
    if (argc > 1)
        servidor = argv[1];

    /* --------------------------------------------------
        Se abre el socket TCP
    ---------------------------------------------------*/
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("No se puede abrir el socket cliente");
        exit(1);
    }

    /* ------------------------------------------------------------------
        Se rellenan los campos de la estructura con la IP del
        servidor y el puerto del servicio que solicitamos
    -------------------------------------------------------------------*/
    bzero(&sockname, sizeof(sockname));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(PUERTO);
    sockname.sin_addr.s_addr = inet_addr(servidor);

    /*
     * Si no era una IP en formato numérico (p. ej. "localhost"), se resuelve
     * el nombre con gethostbyname, que rellena una estructura hostent con la
     * dirección IP en binario (campo h_addr).
     */
    if (sockname.sin_addr.s_addr == INADDR_NONE) {
        host = gethostbyname(servidor);
        if (host == NULL) {
            fprintf(stderr, "No se encuentra el servidor %s\n", servidor);
            exit(1);
        }
        memcpy(&sockname.sin_addr, host->h_addr, host->h_length);
    }

    /* ------------------------------------------------------------------
        Se solicita la conexión con el servidor (aquí se hace el
        establecimiento de conexión de TCP)
    -------------------------------------------------------------------*/
    len_sockname = sizeof(sockname);
    if (connect(sd, (struct sockaddr *) &sockname, len_sockname) == -1) {
        perror("Error de conexión");
        exit(1);
    }

    /* Si el servidor se cae y escribimos en el socket, que no nos mate SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    mostrarAyuda();

    /* Inicializamos las estructuras: vigilamos el teclado (0) y el socket */
    FD_ZERO(&auxfds);
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    FD_SET(sd, &readfds);

    /* ------------------------------------------------------------------
        Se transmite la información
    -------------------------------------------------------------------*/
    do {
        auxfds = readfds;
        salida = select(sd + 1, &auxfds, NULL, NULL, NULL);
        if (salida < 0) {
            perror("Error en select");
            break;
        }

        /* ---------- Tengo mensaje desde el servidor ---------- */
        if (FD_ISSET(sd, &auxfds)) {
            bzero(buffer, sizeof(buffer));

            /*
             * MSG_WAITALL: recv no vuelve hasta tener los MSG_SIZE bytes del
             * bloque completo. Como TCP es un flujo, un recv normal podría
             * devolver medio mensaje; así siempre leemos mensaje a mensaje.
             */
            recibidos = recv(sd, buffer, MSG_SIZE, MSG_WAITALL);

            if (recibidos <= 0) {
                /* 0 = el servidor cerró la conexión; -1 = error */
                printf("\nSe ha perdido la conexión con el servidor\n");
                fin = 1;
            } else {
                /* Mostramos el mensaje; los trozos del tablero ya traen su '\n' */
                largo = strlen(buffer);
                printf("%s", buffer);
                if (largo == 0 || buffer[largo - 1] != '\n')
                    printf("\n");
                fflush(stdout);

                if (strstr(buffer, "Demasiados clientes conectados") != NULL ||
                    strstr(buffer, "Desconexión servidor") != NULL)
                    fin = 1;
            }
        }

        /* ---------- He introducido información por teclado ---------- */
        if (!fin && FD_ISSET(0, &auxfds)) {
            bzero(linea, sizeof(linea));

            if (fgets(linea, sizeof(linea), stdin) == NULL) {
                /* Ctrl+D (fin de la entrada): salimos de forma ordenada */
                strcpy(linea, "SALIR\n");
            }

            /* Si la línea no cabe entera, se avisa y se descarta el resto */
            if (strchr(linea, '\n') == NULL && strlen(linea) == MAX_TEXTO + 1) {
                while ((c = getchar()) != '\n' && c != EOF)
                    ;
                printf("Mensaje demasiado largo (máximo %d caracteres). No se ha enviado.\n",
                       MAX_TEXTO);
                continue;
            }

            /* Quitamos el salto de línea final */
            linea[strcspn(linea, "\r\n")] = '\0';
            if (strlen(linea) == 0)
                continue;   /* línea vacía: no se envía nada */

            /* Se envía SIEMPRE un bloque de MSG_SIZE bytes (texto + relleno de '\0') */
            bzero(buffer, sizeof(buffer));
            strcpy(buffer, linea);
            if (send(sd, buffer, MSG_SIZE, 0) == -1) {
                perror("Error al enviar el mensaje");
                fin = 1;
            }

            if (!fin && strcmp(linea, "SALIR") == 0) {
                /* Esperamos la confirmación del servidor, la mostramos y terminamos */
                bzero(buffer, sizeof(buffer));
                if (recv(sd, buffer, MSG_SIZE, MSG_WAITALL) > 0)
                    printf("%s\n", buffer);
                fin = 1;
            }
        }

    } while (fin == 0);

    close(sd);
    return 0;
}
