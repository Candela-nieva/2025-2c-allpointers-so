
#include "masterUtils.h"

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
    close(fd_sv);
    return 0;
}