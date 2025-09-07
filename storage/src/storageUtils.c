#include "storageUtils.h"

int retardo_operacion;
int retardo_acceso_bloque;

t_log* loggerStorage = NULL;
t_config *config = NULL;
t_config_storage *config_struct = NULL;
char* config_storage;

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_storage)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->puerto_escucha = NULL;
    config_struct->fresh_start = NULL;
    config_struct->punto_montaje = NULL;
    config_struct->retardo_operacion = NULL;
    config_struct->retardo_acceso_bloque = NULL;
    config_struct->log_level = NULL;
}

void cargar_config() {
    config = config_create(config_storage);
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->puerto_escucha = config_get_string_value(config, "PUERTO_ESCUCHA");
    config_struct->fresh_start = config_get_string_value(config, "FRESH_START");
    config_struct->punto_montaje = config_get_string_value(config, "PUNTO_MONTAJE");
    config_struct->retardo_operacion = config_get_string_value(config, "RETARDO_OPERACION");
    config_struct->retardo_acceso_bloque = config_get_string_value(config, "RETARDO_ACCESO_BLOQUE");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");

    retardo_operacion = atoi(config_struct->retardo_operacion);
    retardo_acceso_bloque = atoi(config_struct->retardo_acceso_bloque);
}

// Función para iniciar el logger
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle){
	t_log* nuevo_logger;
	nuevo_logger = log_create(nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger"); // Maneja error si no se puede crear el logger
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

void crear_logger () {
    loggerStorage=iniciar_logger("storage.log","STORAGE",true, log_level_from_string(config_struct->log_level));
}

void iniciar_servidor_multihilo(void)
{
    int fd_sv = crear_servidor(config_struct->puerto_escucha);
    log_info(loggerStorage, "Servidor STORAGE escuchando en puerto %s", config_struct->puerto_escucha);
    while (1)
    {
        int fd_conexion = esperar_cliente(fd_sv, "STORAGE", loggerStorage);
        int operacion = recibir_operacion(fd_conexion);
        if(operacion == HANDSHAKE_WORKER){
            log_info(loggerStorage, "Conexion Exitosa con un nuevo Worker");
            //pthread_t hilo_worker;
            //pthread_create(&hilo_worker, NULL, atender_conexion, NULL);
            //pthread_detach(hilo_worker);
        }
    }
    // Nunca llega acá
    close(fd_sv);
    return;
}