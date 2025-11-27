#include "storageUtils.h"

int main(int argc, char* argv[]) {
    //saludar("storage");
    if(argc < 2)
        return EXIT_FAILURE;
    config_storage = argv[1];
    //signal(SIGINT, terminar_programa_storage);

    inicializar_config();
    cargar_config();
    crear_logger();

    inicializar_montaje();

    iniciar_servidor_multihilo();

    return 0;
}
