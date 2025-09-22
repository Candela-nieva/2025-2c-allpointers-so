#include <utils/protocolo.h>

void saludar(char* quien) {
    printf("Hola desde %s!!\n", quien);
}

void destruir_buffer(t_buffer *buffer) { // Libera la memoria del buffer
    free(buffer->stream); // Libera el stream (puntero a los datos)
    free(buffer);
}

// Envia un buffer genérico
void enviar_buffer(int fd, void *buffer, int tamanio)
{
    send(fd, &tamanio, sizeof(int), 0);
    send(fd, buffer, tamanio, 0);
}

t_paquete *crear_paquete(op_code operacion)
{
    t_paquete *paquete = malloc(sizeof(t_paquete));
    paquete->cod_op = operacion;
    paquete->buffer = malloc(sizeof(t_buffer));
    paquete->buffer->size = 0;
    paquete->buffer->stream = NULL;
    return paquete;
}

void agregar_a_paquete(t_paquete *paquete, void *valor, int tamanio) {
    paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + tamanio);
    memcpy(paquete->buffer->stream + paquete->buffer->size, valor, tamanio);
    paquete->buffer->size += tamanio;
}

// Envia un OK
void enviar_ok(int fd) {
    int ok = 1;
    send(fd, &ok, sizeof(int), 0); // Enviar un entero 1 como OK
}

void agregar_a_paquete_string(t_paquete* paquete, char* cadena, int tamanio) {
    int cadena_length = strlen(cadena); // ee no seria lo mimso que tamanio?
    size_t size = sizeof(int);  // Tamaño en bytes de un entero
    
    // Expandir el tamaño del buffer del paquete para acomodar la longitud de la cadena
    paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + size);
    memcpy(paquete->buffer->stream + paquete->buffer->size, &cadena_length, size);
    paquete->buffer->size += size;

    // Expandir el tamaño del buffer para acomodar la cadena
    paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + tamanio);
    memcpy(paquete->buffer->stream + paquete->buffer->size, cadena, tamanio);
    paquete->buffer->size += tamanio;
}

/*
void agregar_a_paquete_string(t_paquete* paquete, char* cadena) {
    uint32_t length = strlen(cadena) + 1; // incluimos el '\0'

    // Reservar espacio para length
    paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + sizeof(uint32_t));
    memcpy(paquete->buffer->stream + paquete->buffer->size, &length, sizeof(uint32_t));
    paquete->buffer->size += sizeof(uint32_t);

    // Reservar espacio para la cadena
    paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + length);
    memcpy(paquete->buffer->stream + paquete->buffer->size, cadena, length);
    paquete->buffer->size += length;
}
// De esta forma queda consistente: siempre mandás [uint32_t length][char[length]].
*/

void *serializar_paquete(t_paquete *paquete, int bytes)
{
    void *buffer_serializado = malloc(bytes);
    int desplazamiento = 0;

    memcpy(buffer_serializado + desplazamiento, &(paquete->cod_op), sizeof(op_code));
    desplazamiento += sizeof(op_code);

    memcpy(buffer_serializado + desplazamiento, &(paquete->buffer->size), sizeof(int));
    desplazamiento += sizeof(int);

    memcpy(buffer_serializado + desplazamiento, paquete->buffer->stream, paquete->buffer->size);

    return buffer_serializado;
}

void enviar_paquete(t_paquete *paquete, int socket_cliente)
{
    int bytes = sizeof(paquete->cod_op) + sizeof(int) + paquete->buffer->size;
    void *a_enviar = serializar_paquete(paquete, bytes);
    send(socket_cliente, a_enviar, bytes, 0);
    free(a_enviar);
}

void eliminar_paquete(t_paquete *paquete)
{
    destruir_buffer(paquete->buffer);
    free(paquete);
}

//ENVIO Y RECEPCION DE MENSAJES

void enviar_mensaje(char *mensaje, int socket_cliente)
{
    t_paquete *paquete = crear_paquete(MENSAJE);
    agregar_a_paquete(paquete, mensaje, strlen(mensaje) + 1);
    enviar_paquete(paquete, socket_cliente);
    eliminar_paquete(paquete);
}

void recibir_mensaje(t_log *logger, int socket_cliente)
{
    char *buffer = recibir_buffer(socket_cliente);
    log_info(logger, "Me llegó el mensaje: %s", buffer);
    free(buffer);
}
/*
void recibir_proceso(t_log *logger, int socket_cliente)
{
    char *buffer = recibir_buffer(NULL, socket_cliente);
    log_info(logger, "Me llegó el mensaje: %s", buffer);
    free(buffer);
}*/

// RECIBIR

void *recibir_buffer(int socket_cliente) {
    int size;
    recv(socket_cliente, &size, sizeof(int), MSG_WAITALL);
    void *buffer = malloc(size);
    recv(socket_cliente, buffer, size, MSG_WAITALL);
    return buffer;
}

void enviar_operacion(int socket_cliente, int cod_op){
    send(socket_cliente, &cod_op, sizeof(int), 0);
}

int recibir_operacion(int socket_cliente){
    int cod_op;
    recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL);
    if (cod_op >= 0)
        return cod_op;
    return -1;
}

// --- FUNCIONES DE LECTURA DESDE BUFFER ---

// Lee un uint32_t del buffer y avanza el offset
uint32_t buffer_leer_uint32_t(t_buffer* buffer, int *offset) {
    uint32_t value;
    memcpy(&value, buffer->stream + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);
    return value;
}

// Lee un string con length al inicio del buffer y avanza el offset
char* buffer_leer_string(t_buffer* buffer, int* offset) {
    uint32_t length = buffer_leer_uint32_t(buffer, offset);
    char* str = malloc(length);
    memcpy(str, buffer->stream + *offset, length);
    *offset += length;
    return str; // incluye el '\0' si el emisor lo envió
}

// Luego del lado del master deberian serializar con agregar_a_paquete_string
//int32_t length = strlen(path_query) + 1; // incluimos el '\0'
//buffer_add(pack->buffer, &length, sizeof(uint32_t));
//buffer_add(pack->buffer, path_query, length);