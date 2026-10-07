# Makefile de la Práctica 2 de Redes: Dominio Territorial
#
#   make           -> compila servidor y cliente
#   make clean     -> borra ejecutables y ficheros objeto

CC     = gcc
CFLAGS = -Wall -Wextra -g

all: servidor cliente

# El servidor se compone de la parte de red (servidor.c), la lógica del
# juego (juego.c) y la gestión de usuarios (usuarios.c)
servidor: servidor.o juego.o usuarios.o
	$(CC) $(CFLAGS) -o servidor servidor.o juego.o usuarios.o

cliente: cliente.o
	$(CC) $(CFLAGS) -o cliente cliente.o

servidor.o: servidor.c protocolo.h juego.h usuarios.h
	$(CC) $(CFLAGS) -c servidor.c

juego.o: juego.c juego.h protocolo.h
	$(CC) $(CFLAGS) -c juego.c

usuarios.o: usuarios.c usuarios.h
	$(CC) $(CFLAGS) -c usuarios.c

cliente.o: cliente.c protocolo.h
	$(CC) $(CFLAGS) -c cliente.c

clean:
	rm -f *.o servidor cliente
