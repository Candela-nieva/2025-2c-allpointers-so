#include "masterUtils.h"

int main(int argc, char* argv[]) {
    //saludar("master");
    if(argc < 2)
        return EXIT_FAILURE;
    config_master = argv[1];

    inicializar_master();
    return 0;
}