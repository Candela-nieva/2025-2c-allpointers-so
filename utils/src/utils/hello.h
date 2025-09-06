#ifndef UTILS_HELLO_H_
#define UTILS_HELLO_H_

#include <stdlib.h>
#include <stdio.h>

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




#endif
