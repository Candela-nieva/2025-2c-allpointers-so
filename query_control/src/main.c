
#include "queryUtils.h"
//void iniciar_conexion_master();

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
    agregar_a_paquete_string(pack, path_query, strlen(path_query) + 1);
    agregar_a_paquete(pack, &prioridad, sizeof(int));
    enviar_paquete(pack, socket_master);

    log_info(loggerQueryCTRL, "## Solicitud de ejecución de Query: %s, prioridad: %d", 
        path_query, prioridad); // LOG INFO OBLIGATORIO

    eliminar_paquete(pack);

    //escuchar_master(socket_master);

    // Cierre ordenado si por ahora terminamos después del handshake:
    //close(socket_master);
    //log_info(loggerQueryCTRL, "## Query Finalizada - OK");
}


// TODO:
/*
Tenemos que implementar una funcion de escucha de mensajes del master y logearlos
en el formato correcto 
*/

void escuchar_master(int socket_master) {
    while(1) {
        t_paquete* paquete = recibir_paquete(socket_master);
        if(paquete == NULL) {
            log_info(loggerQueryCTRL, "Conexion con el Master perdida");
            break;
        }
        
        switch(paquete->cod_op) {
            case 100 /*MENSAJE DE LECTURA*/: {
                char* file_tag = recibir_string(paquete); // extrer string del paquete
                char* contenido = recibir_string(paquete); // extraer string del paquete
                log_info(loggerQueryCTRL, "## Lectura realizada: Archivo %s, contenido: %s", file_tag, contenido);
                free(file_tag);
                free(contenido);
                break;
            }

            case 101 /*MENSAJE FIN_QUERY*/: {
                char* motivo = recibir_string(paquete); // extraer string del paquete
                log_info(loggerQueryCTRL, "## Query Finalizada - %s", motivo);
                free(motivo);
                eliminar_paquete(paquete);
                //close(socket_master);
                return; // Salimos de la función y terminamos la escucha
            }

            default:
                log_info(loggerQueryCTRL, "Operación desconocida recibida del Master");
                break;
        }
        eliminar_paquete(paquete);
    }
}

int main(int argc, char* argv[]) {
    
    // Luego cuando resivamos los parametros sin hardcodear usamos esto de aca abajo, DESCOMENTAR LUEGO :)
    if(argc < 4) {
        log_info(loggerQueryCTRL, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        //fprintf(stderr, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        return EXIT_FAILURE;
    }

    saludar("query_control");

    // Parametros de entrada
    config_queryCTRL = argv[1];
    char* path_query = argv[2];
    int prioridad = atoi(argv[3]);
    
    if(prioridad < 0){
        log_info(loggerQueryCTRL, "Prioridad invalida: %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    
    //config_queryCTRL = "query.config";
    
    // DUDA: tenemos que pasar el archivo de configuracion por parametro?
    // Si es asi, como lo hacemos? Lo pasamos por linea de comando?
    // En ese caso, al cargar la config, usamos config_queryCTRL en vez de "query.config"

    inicializar_config();
    cargar_config();
    crear_logger();
    
    iniciar_conexion_master(path_query, prioridad);
    
    // Limpieza  manual (idealmente se hace con la funcion terminar_programa, pero en la misma falta el free struct)
    if(loggerQueryCTRL){ log_destroy(loggerQueryCTRL); loggerQueryCTRL = NULL; }
    if(config){ config_destroy(config); config = NULL; }
    if(config_struct){ free(config_struct); config_struct = NULL; }
   
    return 0;
}

// EJEMPLITO DE EJECUTAR QUERY CONTROL
// ./bin/query_control query.config ./query_control/ejemploQuery1 1

//Lectura de archivo: “## Lectura realizada: Archivo <File:Tag>, contenido: <CONTENIDO>”
//Finalización de la Query: “## Query Finalizada - <MOTIVO>”

char* extraer_string_de_paquete(t_buffer* buf, int* cursor){
    // buf->stream es un char* con todo el payload
    // En el envío, vos pusiste el string con '\0' incluido
    char* inicio = (char*)buf->stream + *cursor;
    size_t len = strlen(inicio);        // se detiene en el '\0'
    char* out = malloc(len + 1);
    memcpy(out, inicio, len + 1);       // copia con '\0'
    *cursor += (int)(len + 1);          // avanza cursor
    return out;
}