#include "queryUtils.h"

char* config_queryCTRL;
t_log* loggerQueryCTRL = NULL;
t_config* config = NULL;
t_config_queryctrl* config_struct = NULL;

// ------------------- FUNCIONES DE CONEXION Y COMUNICACION ------------------ //

void iniciar_conexion_master(char* path_query, int prioridad) {
    int socket_master = crear_conexion(config_struct->ip_master, config_struct->puerto_master);
    
    if(socket_master == -1) {
        log_info(loggerQueryCTRL, "Error al crear la conexión con Query Control"); // LOG INFO NO OBLIGATORIO
        exit(EXIT_FAILURE);
    }

    log_info(loggerQueryCTRL, "## Conexión al Master exitosa. IP: %s, Puerto: %s", 
        config_struct->ip_master, config_struct->puerto_master); // LOG INFO OBLIGATORIO
        
    // Armamos paquete de handshake con path_query y prioridad    
    // enviar_operacion(socket_master, HANDSHAKE_QUERY);
    
    t_paquete* pack = crear_paquete(HANDSHAKE_QUERY);
    agregar_a_paquete_string(pack, path_query, strlen(path_query));
    agregar_a_paquete(pack, &prioridad, sizeof(int));
    enviar_paquete(pack, socket_master);

    log_info(loggerQueryCTRL, "## Solicitud de ejecución de Query: %s, prioridad: %d", 
        path_query, prioridad); // LOG INFO OBLIGATORIO

    eliminar_paquete(pack);

    //escuchar_master(socket_master);

    // Cierre ordenado si por ahora terminamos después del handshake:
    close(socket_master); // cierro al salir de la escucha en escuchar_master
    //log_info(loggerQueryCTRL, "## Query Finalizada - OK");
}

void escuchar_master(int socket_master) {
    while(1) {
        t_paquete* paquete = recibir_buffer(socket_master);
        if(paquete == NULL) {
            log_info(loggerQueryCTRL, "Conexion con el Master perdida");
            break;
        }
        
        int offset = 0;
        switch(paquete->cod_op) {
            //MENSAJE DE READ
            case 100: {
                char* file_tag = buffer_leer_string(paquete->buffer, &offset); // extrer string del paquete
                char* contenido = buffer_leer_string(paquete->buffer, &offset); // extraer string del paquete
                log_info(loggerQueryCTRL, "## Lectura realizada: Archivo %s, contenido: %s", file_tag, contenido); // LOG OBLIGATORIO
                free(file_tag);
                free(contenido);
                break;
            }

            //MENSAJE FIN_QUERY
            case 101: {
                char* motivo = buffer_leer_string(paquete->buffer, &offset); // extraer string del paquete
                log_info(loggerQueryCTRL, "## Query Finalizada - %s", motivo); // LOG OBLIGATORIO
                free(motivo);
                eliminar_paquete(paquete);
                //close(socket_master);
                return; // Salimos de la función y terminamos la escucha
            }

            default:
                log_info(loggerQueryCTRL, "Operación desconocida recibida del Master"); // LOG NO OBLIGATORIO
                break;
        }
        eliminar_paquete(paquete);
    }
}

// ------------------- FUNCIONES DE CONFIG Y LOGGER ------------------ //

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_queryctrl)); //Reserva memoria
    
    if(!config_struct){
        log_info(loggerQueryCTRL, "Fallo malloc(config_struct)\n");
        exit(EXIT_FAILURE);
    }
    
    config_struct->modulo = NULL;
    config_struct->ip_master = NULL;
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

    if(!config_queryCTRL){
        log_info(loggerQueryCTRL, "Ruta de config no establecida\n");
        return;
    }

    config = config_create(config_queryCTRL);
    if(!config){
        log_info(loggerQueryCTRL, "No se pudo abrir el archivo de config: %s\n", config_queryCTRL);
        return;
    }

    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->ip_master = config_get_string_value (config, "IP_MASTER");
    config_struct->puerto_master = config_get_string_value(config, "PUERTO_MASTER");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");
    
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
    loggerQueryCTRL=iniciar_logger("queryControl.log", "QUERYCTRL", true, log_level_from_string(config_struct->log_level));
}
