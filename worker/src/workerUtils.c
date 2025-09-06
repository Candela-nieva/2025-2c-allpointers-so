// Inicializa la estructura de configuración
#include "workerUtils.h"
int tam_memoria;
int retardo_memoria;

int socket_storage;
int socket_master;

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_master)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->ip_master = NULL;
    config_struct->puerto_master = NULL;
    config_struct->ip_storage = NULL;
    config_struct->puerto_storage = NULL;
    config_struct->tam_memoria = NULL;
    config_struct->retardo_memoria = NULL;
    config_struct->algoritmo_reemplazo = NULL;
    config_struct->path_scripts = NULL;
    config_struct->log_level = NULL;
}

void cargar_config() {
    config = config_create(master_config);
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->ip_master = config_get_string_value(config, "IP_MASTER");
    config_struct->puerto_master = config_get_string_value(config, "PUERTO_MASTER");
    config_struct->ip_storage = config_get_string_value(config, "IP_STORAGE");
    config_struct->puerto_storage = config_get_string_value(config, "PUERTO_STORAGE");
    config_struct->tam_memoria = config_get_string_value(config, "TAM_MEMORIA");
    config_struct->retardo_memoria = config_get_string_value(config, "RETARDO_MEMORIA");
    config_struct->algoritmo_reemplazo = config_get_string_value(config, "ALGORITMO_REEMPLAZO");
    config_struct->path_scripts = config_get_string_value(config, "PATH_SCRIPTS");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");

    tam_memoria = atoi(config_struct->tam_memoria);
    retardo_memoria = atoi(config_struct->retardo_memoria);
}

// Función para iniciar el logger
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle){
	t_log* nuevo_logger;
	nuevo_logger = log_create( nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger"); // Maneja error si no se puede crear el logger
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

void crear_logger () {
    loggerWorker=iniciar_logger("worker.log","WORKER",true, log_level_from_string(config_struct->log_level));
}

void* iniciar_conexion_master(void* arg){
    int id_worker = *(int*)arg;
    free(arg);
    socket_master = crear_conexion(config_struct->ip, config_struct->puerto_master);
    send(socket_master, &id_worker, sizeof(int), 0);
    log_info(loggerCpu, "Handshake con Master - Worker ID enviado: %d", id_worker);
    return NULL;
}

void* iniciar_conexion_storage(void* arg){ 
    socket_storage = crear_conexion(config_struct->ip, config_struct->puerto_storage);
    if(socket_storage == -1) {
        log_info(loggerCpu, "Error al crear la conexión con Storage");
        pthread_exit(NULL); // Terminar el hilo si hay un error
    }
    enviar_operacion(socket_memoria, HANDSHAKE_MASTER); // Enviar el handshake a Memoria
    return NULL; 
}
