
#include "masterUtils.h"
void* atender_conexion(void* arg);
void atender_QueryControl(int fd);
void atender_Worker(int fd);
void* atender_conexion(void* arg){
    int fd = *(int *)arg;
    free(arg);
    op_code op = recibir_operacion(fd);
    if (op == HANDSHAKE_QUERY)
    {
        log_info(loggerMaster, "## Query Control Conectado - FD del socket: %d", fd);
        atender_QueryControl(fd);
    }
    else if (op == HANDSHAKE_WORKER)
    {
        log_info(loggerMaster, "## Worker Conectado - FD del socket: %d", fd);
        atender_Worker(fd);
    }
    else
    {
        log_info(loggerMaster, "## Handshake inválido (%d) en fd %d", op, fd);
    }
    close(fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    saludar("master");
    config_master = "master.config";
    inicializar_config();
    cargar_config();
    crear_logger();
    int fd_sv = crear_servidor(config_struct->puerto_escucha);
    log_info(loggerMaster, "Servidor MASTER escuchando Peticiones");
    while (1)
    {
        int *peticion = malloc(sizeof(int));
        *peticion = esperar_cliente(fd_sv, "MASTER", loggerMaster);

        pthread_t tid;
        pthread_create(&tid, NULL, atender_conexion, peticion);
        pthread_detach(tid);
    }
    // Nunca llega acá
    close(fd_sv);
    return 0;
}

void atender_QueryControl(int fd){
    log_info(loggerMaster, "CONEXION EXITOSA CON QUERYCONTROL");
    int length_path, prioridad;
    int offset = 0;
    char* path_query;
    void* buffer = recibir_buffer(fd);
    memcpy(&length_path,buffer + offset, sizeof(int));
    offset += sizeof(int);
    path_query = malloc(length_path + 1);

    memcpy(path_query, buffer + offset, length_path);
    path_query[length_path] = '\0';
    offset += length_path;

    memcpy(&prioridad, buffer + offset, sizeof(int));
    free(buffer);
    log_info(loggerMaster, "Query recibido; PATH = %s ; PRIORIDAD = %d", path_query, prioridad);
}

void atender_Worker(int fd){
    log_info(loggerMaster, "CONEXION EXITOSA CON WORKER");
    int id_worker;
    void* buffer = recibir_buffer(fd);
    memcpy(&id_worker, buffer, sizeof(int));
    free(buffer);
    log_info(loggerMaster,"Worker Conectado, ID : %d",id_worker);
}