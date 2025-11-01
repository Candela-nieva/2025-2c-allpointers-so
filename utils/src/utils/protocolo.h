#ifndef UTILS_HELLO_H_
#define UTILS_HELLO_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <stdint.h>
#include <utils/chiches.h>

/**
* @brief Imprime un saludo por consola
* @param quien Módulo desde donde se llama a la función
* @return No devuelve nada
*/
typedef struct{
    int size;
    void* stream;
} t_buffer;

typedef struct{
    int cod_op;
    t_buffer* buffer;
} t_paquete;

typedef enum{
    HANDSHAKE_WORKER,
    HANDSHAKE_QUERY,
    MENSAJE,
    ENVIAR_TAMANIO_BLOQUE,
    EJECUTAR, // Mensaje que se le manda a worker como solicitud de ejecutar una nueva query
    FIN_QUERY, // Mensaje que se le manda a worker para indicarle que no hay mas queries para ejecutar
    MASTER_TO_QC_READ_RESULT, // Mensaje que se le manda a query
    MASTER_TO_QC_END,
    WORKER_TO_MASTER_END, // Mensaje que se le manda a query
    PC_ACTUALIZADO,
    DESALOJO   // Mensaje que se le manda a worker
} op_code;

void saludar(char* quien);
t_paquete *crear_paquete(op_code operacion);
void destruir_buffer(t_buffer *buffer);
void enviar_buffer(int fd, void *buffer, int tamanio);
void agregar_a_paquete(t_paquete *paquete, void *valor, int tamanio);
void agregar_a_paquete_string(t_paquete* paquete, char* cadena, int tamanio);
void enviar_ok(int fd);
void *serializar_paquete(t_paquete *paquete, int bytes);
void enviar_paquete(t_paquete *paquete, int socket_cliente);
void eliminar_paquete(t_paquete *paquete);
void enviar_mensaje(char *mensaje, int socket_cliente);
void recibir_mensaje(t_log *logger, int socket_cliente);
void *recibir_buffer(int socket_cliente);
void enviar_operacion(int socket_cliente, int cod_op);
int recibir_operacion(int socket_cliente);

uint32_t buffer_leer_uint32_t(t_buffer* buffer, int *offset);
char* buffer_leer_string(t_buffer* buffer, int *offset);


#endif
