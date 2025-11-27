
#include "queryUtils.h"

int main(int argc, char* argv[]) {
    
    // Ejemplo de parametros hardcodeados para pruebas rapidas
    //config_queryCTRL = "query.config";
    //char* path_query = "PATH_EJEMPLO";
    //int prioridad = 0;
    if(argc < 4)
        //log_info(loggerQueryCTRL, "Uso: %s <archivo_config> <archivo_query> <prioridad>\n", argv[0]);
        return EXIT_FAILURE;

    // Parametros de entrada
    config_queryCTRL = argv[1];
    char* path_query = argv[2];
    int prioridad = atoi(argv[3]);

    if(prioridad < 0)
        //log_info(loggerQueryCTRL, "Prioridad invalida: %s\n", argv[3]);
        return EXIT_FAILURE;

    //saludar("query_control");
    inicializar_config();
    cargar_config();
    crear_logger();
    
    iniciar_conexion_master(path_query, prioridad);
    
    if(loggerQueryCTRL){ log_destroy(loggerQueryCTRL); loggerQueryCTRL = NULL; }
    if(config){ config_destroy(config); config = NULL; }
    if(config_struct){ free(config_struct); config_struct = NULL; }

    return 0;
}

// EJEMPLITO DE EJECUTAR QUERY CONTROL
// ./bin/query_control query.config ./query_control/ejemploQuery1 1

