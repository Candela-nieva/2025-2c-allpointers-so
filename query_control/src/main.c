
#include "queryUtils.h"
void iniciar_conexion_master();

void iniciar_conexion_master(){
    int socket_master = crear_conexion(config_struct->ip, config_struct->puerto_master);
    if(socket_master == -1) {
        log_info(loggerQueryCTRL, "Error al crear la conexión con Query Control");
    }
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
    saludar("query_control");
    config_queryCTRL = "query.config";
    inicializar_config();
    cargar_config();
    crear_logger();
    iniciar_conexion_master();
    return 0;
}