#include <utils/chiches.h>

// Función Cliente para crear una conexión con una IP y un puerto
int crear_conexion(char* ip, char* puerto){
    int err;
    struct addrinfo hints, *server_info;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(ip, puerto, &hints, &server_info);
    (void)err; // Para evitar warning de variable no usada

    int fd_conexion = socket(server_info->ai_family,
                            server_info->ai_socktype,
                            server_info->ai_protocol);

    err = connect(fd_conexion, server_info->ai_addr, server_info->ai_addrlen);
    if (err != 0) {
        close(fd_conexion);
        freeaddrinfo(server_info);
        return -1;
    }

    freeaddrinfo(server_info);
    return fd_conexion;
}

//Función Servidor que escucha en puerto dado
int crear_servidor(char* puerto) {
    struct addrinfo hints, *server_info;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, puerto, &hints, &server_info);

    int fd_escucha = socket(server_info->ai_family,
                            server_info->ai_socktype,
                            server_info->ai_protocol);

    setsockopt(fd_escucha, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    bind(fd_escucha, server_info->ai_addr, server_info->ai_addrlen);
    listen(fd_escucha, SOMAXCONN);

    freeaddrinfo(server_info);
    return fd_escucha;
}

//Función que espera y acepta una conexión de un cliente
int esperar_cliente (int fd_escucha, char* cliente_nombre, t_log* logger) {
    int fd_conexion = accept(fd_escucha, NULL, NULL);
    //log_info(logger, "Se conecto %s!", cliente_nombre);
    //enviarHandshake (fd_conexion);
    return fd_conexion;
}

// Función para terminar el programa y liberar recursos
void terminar_programa(int conexion, t_log* logger, t_config* config) {
    if (conexion != -1) {
        close(conexion);
    }
    if (logger != NULL) {
        log_destroy(logger);
    }
    if (config != NULL) {
        config_destroy(config);
    }
}

//Función que genera handshake temporalmente entre Kernel e IO
int generar_handshake(int fd, t_log* logger) {
    size_t bytes;
    int32_t result;
    int32_t handshake = 2;
    
    bytes = send(fd, &handshake, sizeof(int32_t), 0);
    (void)bytes; // Para evitar warning de variable no usada
    bytes = recv(fd, &result, sizeof(int32_t), MSG_WAITALL);
    (void)bytes; // Para evitar warning de variable no usada

    if (result == 0) {
        log_info(logger, "Handshake exitoso!\n");
        return 0;
    } else {
        log_info(logger, "Error de Handshake!\n"); 
        return -1;
    }
}

// Función para enviar el handshake al cliente
void enviarHandshake(int fd){

    int32_t handshake;
    recv(fd, &handshake, sizeof(int32_t), MSG_WAITALL);

    int32_t resultOk = 0;
    int32_t resultError = -1;
    
    // Enviar el handshake
    if (handshake == 2 ) {
        send(fd, &resultOk, sizeof(int32_t), 0);
    } else {
        send(fd, &resultError, sizeof(int32_t), 0);
    }
}

// Funcion para obtener tiempos (aging?)
long double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}