#ifndef CHICHES_H
#define CHICHES_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>

/**
* @brief Imprime un saludo por consola
* @param quien Módulo desde donde se llama a la función
* @return No devuelve nada
*/

int crear_conexion(char* ip, char* puerto);
int crear_servidor(char* puerto);
int esperar_cliente (int fd_escucha, char* cliente_nombre, t_log* logger);
void terminar_programa(int conexion, t_log* logger, t_config* config);
int generar_handshake(int fd, t_log* logger);
void enviarHandshake(int fd);
long double get_time_ms();

#endif
