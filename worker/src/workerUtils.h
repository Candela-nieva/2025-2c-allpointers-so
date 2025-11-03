#ifndef WORKERUTILS_H
#define WORKERUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <utils/chiches.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    char* modulo;
    char* ip_master;
    char* puerto_master;
    char* ip_storage;
    char* puerto_storage;
    char* tam_memoria;
    char* retardo_memoria;
    char* algoritmo_reemplazo;
    char* path_scripts;
    char* log_level;
} t_config_worker;

typedef enum {
    CREATE,
    TRUNCATE,
    WRITE,
    READ,
    TAG,
    COMMIT,
    FLUSH,
    DELETE,
    END,
    DESCONOCIDA // Para cualquier instrucción no reconocida
} tipo_instruccion;

// ============== Memoria Interna ================
typedef struct {
    char file_tag[128];     // nombre del file:tag que ocupa este bloque
    int bloque_id;          // ID del bloque dentro del file:tag
    bool modificado;        // indica si el bloque ha sido modificado (1) o no (0) (dirty bit)
    bool en_uso;            // indica si el bloque está en uso (1) o libre (0) (para CLOCK)
    time_t ultima_ref;      // timestamp de la última referencia (para LRU)
    void* datos;            // puntero a los datos del bloque
    bool ocupado;           // indica si el bloque está ocupado (1) o libre (0)
} t_bloque_memoria;

typedef struct {
    t_bloque_memoria* bloques;      // array de bloques de memoria
    int cant_bloques;               // cantidad total de bloques en memoria
    int tamanio_bloque;             // tamaño de cada bloque en bytes
    int tamanio_total;              // tamaño total de la memoria en bytes
    int puntero_clock;              // puntero para el algoritmo CLOCK
    char algoritmo[10];             // CLOCK o LRU 
    pthread_mutex_t mutex;          // mutex para sincronización de acceso
} t_memoria_interna;

// ============== Tabla de Páginas (por cada File:Tag)================

typedef struct {
    int num_pagina;
    int marco;
    bool presente;
} t_pagina;

typedef struct {
    char file_tag[128];
    t_list* paginas;        // lista de t_pagina* (commons/list.h)
    int cant_paginas;
} t_tabla_paginas;


// =========== Variables globales ==================

extern t_log* loggerWorker;
extern t_config* config;
extern t_config_worker* config_struct; 
extern char* config_worker;

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void esperar_queries();
void* iniciar_conexion_storage(void* arg);
void *manejar_ejecutar(void* buffer);
void* iniciar_conexion_master(void* arg);
static void trim_newline(char* s);
static bool ejecutar_instruccion(const char* instruccion, int qid, int pc);
void ejecutar_query(int pc_inicial, const char* archivo_relativo, int qid);
tipo_instruccion obtener_instruccion(const char* op);
static void notificar_fin_query_a_master(int qid, const char* motivo);
void ejecutar_write(char* tag, int direccionBase, char* contenido, int qid);
t_bloque_memoria* buscar_bloque(char* tag, int bloque_id);
int seleccionar_bloque_victima();
int reemplazo_clock_modificado();
int reemplazo_lru();
void enviar_bloque_a_storage(t_bloque_memoria* bloque);
void solicitar_bloque_a_storage(char* tag, int bloque_id, t_bloque_memoria* destino);
#endif