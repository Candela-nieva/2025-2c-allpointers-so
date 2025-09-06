// Inicializa la estructura de configuración

int retardo_operacion;
int retardo_acceso_bloque;
#include "storageUtils.h"
void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_master)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->puerto_escucha = NULL;
    config_struct->fresh_start = NULL;
    config_struct->punto_montaje = NULL;
    config_struct->retardo_operacion = NULL;
    config_struct->retardo_acceso_bloque = NULL;
    config_struct->log_level = NULL;
}

void cargar_config() {
    config = config_create(master_config);
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
	nuevo_logger = log_create( nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger"); // Maneja error si no se puede crear el logger
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

void crear_logger () {
    loggerStorage=iniciar_logger("storage.log","STORAGE",true, log_level_from_string(config_struct->log_level));
}
