#include <utils/protocolo.h>
#include "workerUtils.h"
int main(int argc, char* argv[]) {

     /*if(argc < 3) { // mientras usamos printf no esta mal, pero al usar log_info necesitamos el logger inicializado
        printf("Uso: %s <path_config> <worker_id>\n", argv[0]);
        return EXIT_FAILURE;
    }*/

    // Parametros de entrada
    config_worker = "worker.config";
    int worker_id = 1;

    //config_worker = argv[1];
    //int worker_id = atoi(argv[2]);
    saludar("worker");

    inicializar_config();
    cargar_config();
    crear_logger();

    int* id = malloc(sizeof(int));
    *id = worker_id;

    // creo que no se libera el espacio pedido para el id de worker ¿?
    
    pthread_t hilo_storage, hilo_master;
    pthread_create(&hilo_storage, NULL, iniciar_conexion_storage, id);
    pthread_create(&hilo_master, NULL, iniciar_conexion_master, id);
    pthread_join(hilo_master, NULL); // join --> asegura que el programa no finalice hasta que ambos hilos terminen (o sea, hasta que Master o Storage corten la conexión)
    pthread_join(hilo_storage, NULL);

    liberar_recursos_worker();
    free(id);
    return 0;
}

// EJEMPLITO DE EJECUTAR WORKER
// ./bin/worker worker.config 1
