#include <utils/hello.h>
#include "workerUtils.h"
int main(int argc, char* argv[]) {
    saludar("worker");

    //char* path_config = strdup(argv[1]);
    //int worker_id = atoi(argv[2]);

    inicializar_config();
    cargar_config();
    crear_logger();
    int* id = malloc(sizeof(int));
    *id = 1/*worker_id*/;
    pthread_create(&hilo_memoria, NULL, iniciar_conexion_storage, NULL); // Crear hilo de MEMORIA a CPU
    pthread_create(&hilo_kernel, NULL, iniciar_conexion_master, id);   // Crear hilo de KERNEL a CPU dispach
    return 0;
}
