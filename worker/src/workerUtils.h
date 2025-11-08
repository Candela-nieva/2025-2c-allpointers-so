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
#include <commons/collections/dictionary.h>
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

// ============== Memoria Interna ================
typedef struct {
    char file_tag[128];     // nombre del file:tag que ocupa este marco (cadena vacia si esta libre)
    //int marco_id;          // ID del marco dentro del file:tag
    int pagina_logica;      
    bool modificado;        // indica si el marco ha sido modificado (1) o no (0) (dirty bit)
    bool en_uso;            // indica si el marco está en uso (1) o libre (0) (para CLOCK)
    time_t ultima_ref;      // timestamp de la última referencia (para LRU)
    //void* datos;          // puntero a los datos del bloque // CREO QUE NO VA
    bool ocupado;           // indica si el marco está ocupado (1) o libre (0)
} t_marco;

typedef struct {
    void* buffer;                   // unico malloc que contiene todos los marcos
    t_marco* marcos;                // array de marcos de memoria
    int cant_marcos;                // cantidad total de marcos en memoria
    int tamanio_marco;              // tamaño de cada marco en bytes
    int tamanio_total;              // tamaño total de la memoria en bytes
    int puntero_clock;              // puntero para el algoritmo CLOCK
    char algoritmo[10];             // CLOCK o LRU 
    pthread_mutex_t mutex;          // mutex para sincronización de acceso
} t_memoria_interna;

// ============== Tabla de Páginas (por cada File:Tag)================

typedef struct {
    int num_pagina;     // numero de pagina
    int marco;          // numero de marco
    bool presente;      // bit de presenncia (1 = en memoria)
    bool modificado;    // bit de modificado (1 = modificado)
    bool uso;           // bit de uso (1 = usado recientemente - para CLOCK)
    time_t ultima_ref;  // timestamp de la última referencia (para LRU)
} t_pagina;

typedef struct {
    char file_tag[128];     // identificador del file:tag
    t_list* paginas;        // lista de t_pagina* (commons/list.h)
    //int cant_paginas;
} t_tabla_paginas;


// =========== Variables globales ==================

extern t_log* loggerWorker;
extern t_config* config;
extern t_config_worker* config_struct; 
extern char* config_worker;
extern t_dictionary* tabla_de_paginas;

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

//t_bloque_memoria* buscar_bloque(char* tag, int bloque_id);
t_pagina* manejar_page_fault(char* file_tag, int pagina_logica, t_tabla_paginas* tabla, int qid);

void inicializar_memoria_interna();
//============= EJECUTAR INSTRUCCIONES ==============
void ejecutar_create(char* nombreArch, char* nombreTag, int qid);
void ejecutar_truncate(char* nombreArch, char* nombreTag, int qid, int nuevo_tam);
void ejecutar_write(char* nombreArch, char* nombreTag, int direccionBase, char* contenido, int qid);

int seleccionar_victima(int quid);
int seleccionar_bloque_victima();
int reemplazo_clock_modificado();
int reemplazo_lru();

//void enviar_pagina_a_storage(t_marco* bloque);
//void solicitar_pagina_a_storage(char* tag, int bloque_id, t_marco* destino);
void* direccion_fisica_marco(int marco_id);

void inicializar_tablas_paginas();
void liberar_tablas_paginas();
t_tabla_paginas* obtener_o_crear_tabla_paginas(char * file_tag);
t_pagina* buscar_pagina(t_tabla_paginas* tabla, int num_pagina);
#endif