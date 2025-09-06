#include <utils/hello.h>

int main(int argc, char* argv[]) {
    saludar("query_control");
    inicializar_config();
    cargar_config();
    crear_logger();
    return 0;
}
