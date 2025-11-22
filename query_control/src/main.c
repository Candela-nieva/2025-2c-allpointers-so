
#include "queryUtils.h"

int main(int argc, char* argv[]) {
    
  
    
    // Ejemplo de parametros hardcodeados para pruebas rapidas
    config_queryCTRL = "query.config";
    char* path_query = "testInicial";
    int prioridad = 1;
    
    // Parametros de entrada
    //config_queryCTRL = argv[1];
    //char* path_query = argv[2];
    //int prioridad = atoi(argv[3]);

    saludar("query_control");
    inicializar_config();
    cargar_config();
    crear_logger();
    
    /*if(argc < 4) {
        log_info(loggerQueryCTRL, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        return EXIT_FAILURE;
    }*/

    if(prioridad < 0){
        log_info(loggerQueryCTRL, "Prioridad invalida: %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    
    
    iniciar_conexion_master(path_query, prioridad);
    
    if(loggerQueryCTRL){ log_destroy(loggerQueryCTRL); loggerQueryCTRL = NULL; }
    if(config){ config_destroy(config); config = NULL; }
    if(config_struct){ free(config_struct); config_struct = NULL; }

    return 0;
}

// EJEMPLITO DE EJECUTAR QUERY CONTROL
// ./bin/query_control query.config ./query_control/ejemploQuery1 1


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