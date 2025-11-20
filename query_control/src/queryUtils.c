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

    escuchar_master(socket_master);

    // Cierre ordenado si por ahora terminamos después del handshake:
    //close(socket_master); // cierro al salir de la escucha en escuchar_master
    //log_info(loggerQueryCTRL, "## Query Finalizada - OK");
}

void escuchar_master(int socket_master) {
    while(1) {
        //SOCKET 
        op_code operacion = recibir_operacion(socket_master);
        switch(operacion) {
            //MENSAJE DE READ
            case MASTER_TO_QC_READ_RESULT:
                recibir_mensaje_read(socket_master);
                break;
            //MENSAJE FIN_QUERY
            case MASTER_TO_QC_END:
                recibir_mensaje_exit(socket_master);
                close(socket_master);
                return; // Salimos de la función y terminamos la escucha*/
            case -1:
                log_info(loggerQueryCTRL, "El Master ha cerrado la conexión");
                close(socket_master);
                return;
            default:
                log_info(loggerQueryCTRL, "Operación desconocida recibida del Master"); // LOG NO OBLIGATORIO
                return;
        }
        //eliminar_paquete(paquete);
    }
}

void recibir_mensaje_read(int socket_master){
    void *buffer = recibir_buffer(socket_master);
    char* contenido, nombreArch, nombreTag;
    int len_contenido, len_nombreArch, len_nombreTag;

    int offset = 0;

    //Lectura de contenido
    memcpy(&(len_contenido),buffer + offset, sizeof(int));
    offset += sizeof(int);
    contenido = malloc(len_contenido + 1);

    memcpy(contenido,buffer + offset, len_contenido);
    contenido[len_contenido] = '\0';

    //Lectura de nombre del Archivo

    memcpy(&(len_nombreArch),buffer + offset + len_contenido, sizeof(int));
    offset += sizeof(int);
    nombreArch = malloc(len_nombreArch + 1);

    memcpy(nombreArch,buffer + offset + len_contenido, len_nombreArch);
    nombreArch[len_nombreArch] = '\0';

    //Lectura de nombre del Tag

    memcpy(&(len_nombreTag),buffer + offset + len_contenido + len_nombreArch, sizeof(int));
    offset += sizeof(int);
    nombreTag = malloc(len_nombreTag + 1);

    memcpy(nombreTag,buffer + offset + len_contenido + len_nombreArch, len_nombreTag);
    nombreTag[len_nombreTag] = '\0';

    log_info(loggerQueryCTRL, "## Lectura realizada: File - %s : %s, contenido: %s", nombreArch, nombreTag, contenido); // LOG OBLIGATORIO
}

void recibir_mensaje_exit(int socket_master){
    int motivo_int = recibir_operacion(socket_master)

    t_motivo motivo = (t_motivo) motivo_int;

    const char* motivo = obtener_motivo_string(motivo);

    // Log obligatorio
    log_info(logger, "## Query Finalizada - %s", motivo);

    // Cerrar la conexión y terminar el Query Control
    close(socket_master);
}

const char* obtener_motivo_string(t_motivo motivo) {
    switch (motivo) {
        case RESULTADO_OK:                 return "RESULTADO EXITOSO"
        case ERROR_FILE_INEXISTENTE:       return "FILE_INEXISTENTE";
        case ERROR_TAG_INEXISTENTE:        return "TAG_INEXISTENTE";
        case ERROR_FILE_PREEXISTENTE:      return "FILE_YA_EXISTE";
        case ERROR_TAG_PREEXISTENTE:       return "TAG_YA_EXISTE";
        case ERROR_LECTURA_NO_PERMITIDA:   return "LECTURA_RECHAZADA_COMMITED";
        case ERROR_ESCRITURA_NO_PERMITIDA: return "ESCRITURA_RECHAZADA_COMMITED";
        case ERROR_ESPACIO_INSUFICIENTE:   return "STORAGE_SIN_ESPACIO";
        case ERROR_FUERA_DE_LIMITE:        return "ACCESO_FUERA_DE_LIMITE";
        case ERROR_NO_PUDO_ABRIR_ARCHIVO:  return "FALLO_APERTURA_ARCHIVO";
        case ERROR_LECTURA_FALLIDA:        return "FALLO_LECTURA_FISICA";
        case ERROR_LINK_FALLIDO:           return "FALLO_CREACION_LINK";
        case ERROR_PAGE_FAULT:             return "FALLO_PAGE_FAULT";
        case ERROR_TAMANIO_NO_MULTIPLO:    return "TAMANIO_NO_MULTIPLO";
        case DESCONEXION_WORKER:           return "WORKER_DESCONECTADO";
        default:                           return "MOTIVO_DESCONOCIDO";
    }
}

// ------------------- FUNCIONES DE CONFIG Y LOGGER ------------------ //

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_queryctrl)); //Reserva memoria
    
    if(!config_struct){
        log_info(loggerQueryCTRL, "Fallo malloc(config_struct)");
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
        fprintf(stderr, "Ruta de config no establecida\n"); //aun no existe el logger
        return;
    }

    config = config_create(config_queryCTRL);
    if(!config){
        fprintf(stderr, "No se pudo abrir el archivo de config: %s\n", config_queryCTRL); //aun no existe el logger
        return;
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
