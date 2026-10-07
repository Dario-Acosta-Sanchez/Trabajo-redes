/*
 * usuarios.c
 * ----------
 * Implementación de la gestión de usuarios.
 *
 * Mantenemos una copia de todos los usuarios en memoria (vector 'usuarios')
 * para buscarlos rápido, y cada vez que alguien se registra lo añadimos
 * también al final del fichero para que persista entre ejecuciones.
 */

#include <stdio.h>
#include <string.h>
#include "usuarios.h"

typedef struct {
    char nombre[MAX_NOMBRE];
    char password[MAX_NOMBRE];
} Usuario;

/* static: estas variables solo se ven dentro de este fichero */
static Usuario usuarios[MAX_USUARIOS];
static int numUsuarios = 0;

void cargarUsuarios(void)
{
    FILE *f = fopen(FICHERO_USUARIOS, "r");

    if (f == NULL) {
        printf("No existe %s: se creará al registrar el primer usuario\n", FICHERO_USUARIOS);
        return;
    }

    /* %49s: como mucho 49 caracteres + '\0' = MAX_NOMBRE, así no se desborda */
    while (numUsuarios < MAX_USUARIOS &&
           fscanf(f, "%49s %49s", usuarios[numUsuarios].nombre,
                  usuarios[numUsuarios].password) == 2) {
        numUsuarios++;
    }

    fclose(f);
    printf("Cargados %d usuarios de %s\n", numUsuarios, FICHERO_USUARIOS);
}

/* Devuelve la posición del usuario en el vector, o -1 si no existe */
static int buscarUsuario(const char *nombre)
{
    int i;

    for (i = 0; i < numUsuarios; i++)
        if (strcmp(usuarios[i].nombre, nombre) == 0)
            return i;
    return -1;
}

int existeUsuario(const char *nombre)
{
    return buscarUsuario(nombre) != -1;
}

int comprobarPassword(const char *nombre, const char *password)
{
    int i = buscarUsuario(nombre);

    return i != -1 && strcmp(usuarios[i].password, password) == 0;
}

int registrarUsuario(const char *nombre, const char *password)
{
    FILE *f;

    /* Comprobamos que caben en los campos de la estructura */
    if (strlen(nombre) >= MAX_NOMBRE || strlen(password) >= MAX_NOMBRE)
        return REG_INVALIDO;

    /* Control de colisiones: no puede haber dos usuarios con el mismo nombre */
    if (existeUsuario(nombre))
        return REG_EXISTE;

    if (numUsuarios >= MAX_USUARIOS)
        return REG_ERROR;

    /* "a" = append: se escribe al final del fichero sin borrar lo que había */
    f = fopen(FICHERO_USUARIOS, "a");
    if (f == NULL)
        return REG_ERROR;
    fprintf(f, "%s %s\n", nombre, password);
    fclose(f);

    strcpy(usuarios[numUsuarios].nombre, nombre);
    strcpy(usuarios[numUsuarios].password, password);
    numUsuarios++;
    return REG_OK;
}
