// Inicializa la estructura de configuración
#include "workerUtils.h"
int tam_memoria;
int retardo_memoria;
t_log* loggerWorker = NULL;
t_config* config = NULL;
t_config_worker* config_struct = NULL;
int socket_storage;
int socket_master;
char* config_worker;

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_worker)); //Reserva memoria
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
    config = config_create(config_worker);
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
    log_info(loggerWorker, "INTENTO HANDSHAKE CON MASTER");
    socket_master = crear_conexion(config_struct->ip_master, config_struct->puerto_master);
    t_paquete* paquete = crear_paquete(HANDSHAKE_WORKER);
    agregar_a_paquete(paquete, &id_worker, sizeof(int));
    enviar_paquete(paquete, socket_master);
    eliminar_paquete(paquete);
    log_info(loggerWorker, "Handshake con Master - Worker ID enviado: %d", id_worker);
    esperar_queries();
    return NULL;
}

void esperar_queries(){
    int cod_op = recibir_operacion(socket_master);
    if(cod_op != EJECUTAR){
        log_info(loggerWorker, "Error al recibir cod_op de Master, se esperaba EJECUTAR");
        return;
    }
    int offset = 0;
    int pc, tamarch;
    char *archivo;
    void *buffer = recibir_buffer(socket_master);
    memcpy(&(pc), buffer, sizeof(int));
    offset += sizeof(int);
    memcpy(&(tamarch), buffer + offset, sizeof(int));
    offset += sizeof(int);
    archivo = malloc(tamarch + 1);
    memcpy(archivo, buffer + offset, tamarch);
    archivo[tamarch] = '\0';
    free(buffer);
    log_info(loggerWorker, "Se recibio el PC %d correspondiente al archivo con path %s", pc, archivo);
    free(archivo);
}

void* iniciar_conexion_storage(void* arg){ 
    socket_storage = crear_conexion(config_struct->ip_storage, config_struct->puerto_storage);
    if(socket_storage == -1) {
        log_info(loggerWorker, "Error al crear la conexión con Storage");
        pthread_exit(NULL); // Terminar el hilo si hay un error
    }
    enviar_operacion(socket_storage, HANDSHAKE_WORKER); // Enviar el handshake a Memoria
    //recibir tamanio de pags
    int op = recibir_operacion(socket_storage);
    if(op != ENVIAR_TAMANIO_BLOQUE)
        log_info(loggerWorker, "ERROR AL RECIBIR COD DE OP, %d", op);
    
    int tamanio_pag;
    void* buffer = recibir_buffer(socket_storage);
    memcpy(&tamanio_pag, buffer, sizeof(int));
    free(buffer);
    log_info(loggerWorker, "Tamanio de Pag recibido de Storage: %d", tamanio_pag);
    return NULL; 
}
