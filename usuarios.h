/*
 * usuarios.h
 * ----------
 * Gestión de los usuarios registrados en el juego (REGISTRO, USUARIO, PASSWORD).
 *
 * Los usuarios se guardan en el fichero de texto FICHERO_USUARIOS, una línea
 * por usuario con el formato "usuario password". Así los registros no se
 * pierden al cerrar el servidor.
 */

#ifndef USUARIOS_H
#define USUARIOS_H

#define FICHERO_USUARIOS "usuarios.txt"
#define MAX_USUARIOS     100
#define MAX_NOMBRE       50     /* longitud máxima de usuario y password (incluido '\0') */

/* Resultados posibles de registrarUsuario() */
#define REG_OK       0
#define REG_EXISTE   1   /* ya hay un usuario con ese nombre */
#define REG_INVALIDO 2   /* usuario o password demasiado largos */
#define REG_ERROR    3   /* no caben más usuarios o no se pudo escribir el fichero */

/* Lee el fichero de usuarios y los carga en memoria (se llama al arrancar) */
void cargarUsuarios(void);

/* Devuelve 1 si existe un usuario con ese nombre */
int existeUsuario(const char *nombre);

/* Devuelve 1 si el usuario existe y la password es correcta */
int comprobarPassword(const char *nombre, const char *password);

/* Registra un usuario nuevo (en memoria y en el fichero). Devuelve REG_* */
int registrarUsuario(const char *nombre, const char *password);

#endif
