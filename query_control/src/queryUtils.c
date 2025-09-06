#include "queryUtils.h"
char* config_queryCTRL;
t_log* loggerQueryCTRL = NULL;
t_config* config = NULL;
t_config_queryctrl* config_struct = NULL;
char* config_queryCTRL = NULL;

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_queryctrl)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->ip = NULL;
    config_struct->puerto_master = NULL;
    config_struct->log_level = NULL;
}

void cargar_config() {
    /*if(argc == 3) {
        master_config = strdup(argv[1]);
        archivo_query = strdup(argv[2]); // Copia el nombre del archivo query
        prioridad = atoi(argv[3]); // Convierte el tercer argumento a entero
    } else {
        archivo_query = NULL;
        prioridad = 0; // No hay argumentos, no se inicializa el proceso
    }*/

    config = config_create(config_queryCTRL);
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->ip = config_get_string_value (config, "IP_MASTER");
    config_struct->puerto_master = config_get_string_value(config, "PUERTO_MASTER");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");
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
    loggerQueryCTRL=iniciar_logger("queryControl.log","QUERYCTRL",true, log_level_from_string(config_struct->log_level));
}
