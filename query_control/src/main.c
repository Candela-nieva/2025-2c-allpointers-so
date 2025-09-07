
#include "queryUtils.h"
void iniciar_conexion_master();

void iniciar_conexion_master(){
    int socket_master = crear_conexion(config_struct->ip_master, config_struct->puerto_master);
    
    if(socket_master == -1) {
        log_info(loggerQueryCTRL, "Error al crear la conexión con Query Control"); // LOG INFO NO OBLIGATORIO
        exit(EXIT_FAILURE);
    }

    log_info(loggerQueryCTRL, "## Conexión al Master exitosa. IP: %s, Puerto: %s", 
        config_struct->ip_master, config_struct->puerto_master);
        
    // Armamos paquete de handshake con path_query y prioridad    
    // enviar_operacion(socket_master, HANDSHAKE_QUERY);
    char* path_query = "aaaa";
    int prioridad = 1;
    t_paquete* pack = crear_paquete(HANDSHAKE_QUERY);
    agregar_a_paquete_string(pack, path_query, strlen(path_query));
    agregar_a_paquete(pack, &prioridad, sizeof(int));
    enviar_paquete(pack, socket_master);
    return;
}

int main(int argc, char* argv[]) {
    
    /*
    // Luego cuando resivamos los parametros sin hardcodear usamos esto de aca abajo, DESCOMENTAR LUEGO :)
    if(argc < 4) {
        return EXIT_FAILURE;
    }

    // Parametros de entrada
    config_queryCTRL = argv[1];
    char* path_query = argv[2]:
    int prioridad = atoi(argv[3]);
    */

    saludar("query_control");
    config_queryCTRL = "query.config";
    inicializar_config();
    cargar_config();
    crear_logger();
    
    iniciar_conexion_master();
    return 0;
}