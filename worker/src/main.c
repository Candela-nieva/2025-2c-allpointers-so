#include <utils/hello.h>
#include "workerUtils.h"
int main(int argc, char* argv[]) {
    saludar("worker");

    //char* path_config = strdup(argv[1]);
    //int worker_id = atoi(argv[2]);
    config_worker = "worker.config";
    inicializar_config();
    cargar_config();
    crear_logger();
    int* id = malloc(sizeof(int));
    *id = 1/*worker_id*/;
    pthread_t hilo_storage, hilo_master;
    //pthread_create(&hilo_storage, NULL, iniciar_conexion_storage, NULL);
    pthread_create(&hilo_master, NULL, iniciar_conexion_master, id);
    pthread_join(hilo_master, NULL);
    return 0;
}
