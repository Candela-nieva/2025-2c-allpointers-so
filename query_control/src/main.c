
#include "queryUtils.h"

// TODO:
/*
Tenemos que implementar una funcion de escucha de mensajes del master y logearlos
en el formato correcto 
*/


int main(int argc, char* argv[]) {
    
    if(argc < 4) {
        log_info(loggerQueryCTRL, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        //fprintf(stderr, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Ejemplo de parametros hardcodeados para pruebas rapidas
    //config_queryCTRL = "query.config";
    //char* path_query = "aaaaa";
    //int prioridad = 1;
    
    // Parametros de entrada
    config_queryCTRL = argv[1];
    char* path_query = argv[2];
    int prioridad = atoi(argv[3]);
    
    if(prioridad < 0){
        log_info(loggerQueryCTRL, "Prioridad invalida: %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    
    //config_queryCTRL = "query.config";
    
    // DUDA: tenemos que pasar el archivo de configuracion por parametro? SI
    // Si es asi, como lo hacemos? Lo pasamos por linea de comando?
    // En ese caso, al cargar la config, usamos config_queryCTRL en vez de "query.config"

    saludar("query_control");
    //config_queryCTRL = "query.config";
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