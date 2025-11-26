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
#include <semaphore.h>
#include <limits.h> // para LONG_MAX

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
    char file_tag[128];      // nombre del file:tag que ocupa este marco (cadena vacia si esta libre)
    int pagina_logica;      
    bool ocupado;            // indica si el marco está ocupado (1) o libre (0)
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
} t_tabla_paginas;


// =========== Variables globales ==================

extern t_log* loggerWorker;
extern t_config* config;
extern t_config_worker* config_struct; 
extern char* config_worker;
extern t_dictionary* tablas_de_paginas;
extern sem_t sem_storage_ready;

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void* iniciar_conexion_storage(void* arg);
void* iniciar_conexion_master(void* arg);
void inicializar_memoria_interna();

void esperar_queries();
void *manejar_ejecutar(void* buffer);
void notificar_fin_query_a_master(int qid, int motivo_op_code);

void manejar_errores(t_motivo motivo, int qid);
void deserializar_fileTag(char* fileTag, char **file, char **tag);


//============= EJECUTAR INSTRUCCIONES ==============
void trim_newline(char* s);
bool ejecutar_instruccion(const char* instruccion, int qid, int pc, t_list* archivos_abiertos);
void ejecutar_query(int pc_inicial, const char* archivo_relativo, int qid);
tipo_instruccion obtener_instruccion(const char* op);

t_motivo ejecutar_create(char* file_tag, int qid);
t_motivo ejecutar_truncate(char* file_tag, int qid, int nuevo_tam);
t_motivo ejecutar_write(char* file_tag, int direccionBase, char* contenido, int qid);
t_motivo ejecutar_read(char* file_tag, int direccionBase, int tamanio, int qid);
t_motivo ejecutar_delete(char* file_tag, int qid);
t_motivo ejecutar_commit(char* file_tag, int qid);
t_motivo ejecutar_tag(char* origen, char* destino, int qid);
t_motivo ejecutar_flush(char* file_tag, int qid);

//============ AUXILIARES PARA WRITE Y READ ==================
t_motivo enviar_bloque_a_storage(int qid, char* file_tag, int nro_pagina_logica, void* contenido);
t_motivo solicitar_bloque_a_storage(int qid, char* file_tag, int pagina_logica, t_marco* destino);
void* direccion_fisica_marco(int marco_id);

//===================== TABLAS DE PAGINAS ===================
t_pagina* manejar_page_fault(char* file_tag, int pagina_logica, t_tabla_paginas* tabla, int qid, t_motivo *motivo);
void inicializar_tablas_paginas();
void liberar_tablas_paginas();
t_tabla_paginas* obtener_o_crear_tabla_paginas(char * file_tag);
t_pagina* buscar_pagina(t_tabla_paginas* tabla, int num_pagina);
t_marco* obtener_marco_de_pagina(char* file_tag, int num_pagina);
int obtener_indice_marco_de_pagina(char* file_tag, int num_pagina);

//========== AUXILIARES PARA FLUSH ================
bool es_mismo_archivo(void* elemento);
void registrar_archivo_abierto(t_list* lista, char* file_tag);

//============= ALGORITMOS DE REEMPLAZO
int seleccionar_victima(int quid);
int seleccionar_bloque_victima();
int reemplazo_clock_modificado();
int reemplazo_lru();
void liberar_recursos_worker();

#endif