/*
 * servidor.c - Servidor de Chat Multi-Sala con Funcionalidades Avanzadas
 * 
 * Este programa implementa un servidor completo de chat multi-sala que utiliza
 * colas de mensajes System V para la comunicación entre múltiples clientes.
 * Incluye funcionalidades avanzadas como historial persistente, gestión de
 * usuarios y comandos administrativos.
 * 
 * Funcionalidades principales:
 * - Gestión automática de múltiples salas de chat
 * - Soporte para múltiples usuarios por sala (hasta 20)
 * - Sistema de historial persistente en archivos
 * - Comandos administrativos (lista de salas y usuarios)
 * - Entrada y salida dinámica de usuarios de salas
 * - Distribución eficiente de mensajes
 * - Limpieza automática de recursos al terminar
 * 
 * Protocolo de mensajes soportado:
 * - Tipo 1 (JOIN):  Cliente solicita unirse a una sala
 * - Tipo 2 (RESP):  Respuesta del servidor al cliente
 * - Tipo 3 (MSG):   Mensaje de chat para distribuir
 * - Tipo 4 (CHAT):  Mensaje distribuido por el servidor
 * - Tipo 5 (LEAVE): Cliente abandona sala actual
 * - Tipo 6 (USERS): Solicitud de lista de usuarios en sala
 * - Tipo 7 (LIST):  Solicitud de lista de salas disponibles
 * 
 * Archivos generados:
 * - <nombre_sala>.txt: Historial de mensajes por sala
 */

#include <stdio.h>        // entrada/salida estándar
#include <stdlib.h>       // funciones de utilidad general
#include <string.h>       // manipulación de strings
#include <sys/types.h>    // tipos de datos del sistema
#include <sys/ipc.h>      // comunicación entre procesos
#include <sys/msg.h>      // colas de mensajes System V
#include <unistd.h>       // funciones estándar de Unix
#include <signal.h>       // manejo de señales del sistema
#include <errno.h>        // códigos de error del sistema

/* ==================== CONSTANTES Y CONFIGURACIÓN ==================== */
#define MAX_SALAS 10                    // Número máximo de salas de chat simultáneas
#define MAX_USUARIOS_POR_SALA 20        // Límite de usuarios por sala individual
#define MAX_TEXTO 256                   // Longitud máxima de un mensaje de texto
#define MAX_NOMBRE 50                   // Longitud máxima para nombres de usuario y salas

/* ==================== ESTRUCTURAS DE DATOS ==================== */

/**
 * Estructura de mensaje para comunicación cliente-servidor
 * 
 * Utilizada para todos los tipos de comunicación entre clientes y servidor.
 * Debe coincidir exactamente con la estructura del cliente para garantizar
 * compatibilidad en la transmisión de datos.
 */
struct mensaje {
    long mtype;                     // Tipo de mensaje (1-7, ver protocolo arriba)
    int reply_qid;                  // ID de cola privada del cliente (para respuestas)
    char remitente[MAX_NOMBRE];     // Nombre del usuario que envía el mensaje
    char texto[MAX_TEXTO];          // Contenido del mensaje o datos adicionales
    char sala[MAX_NOMBRE];          // Nombre de la sala objetivo o actual
};

/**
 * Estructura que representa una sala de chat en memoria del servidor
 * 
 * Mantiene toda la información necesaria para gestionar una sala:
 * usuarios conectados, su información de contacto y recursos asociados.
 */
struct sala {
    char nombre[MAX_NOMBRE];                            // Nombre identificador único de la sala
    int cola_id;                                        // ID de cola System V asociada a la sala
    int num_usuarios;                                   // Contador actual de usuarios en la sala
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];  // Array de nombres de usuarios conectados
    int usuarios_qid[MAX_USUARIOS_POR_SALA];           // Array de IDs de colas privadas de usuarios
};

/* ==================== VARIABLES GLOBALES ==================== */
struct sala salas[MAX_SALAS];       // Array de todas las salas de chat disponibles
int num_salas = 0;                  // Contador actual de salas activas
int cola_global = -1;               // ID de la cola global donde llegan todos los mensajes

/* ==================== PROTOTIPOS DE FUNCIONES ==================== */
int crear_sala(const char *nombre);                                        // Crea nueva sala de chat
int buscar_sala(const char *nombre);                                       // Busca sala por nombre
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario);  // Agrega usuario a sala
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg);        // Distribuye mensaje en sala
void guardar_historial(int indice_sala, struct mensaje *msg);             // Guarda mensaje en archivo
void limpiar_colas_y_salir(int signo);                                    // Limpia recursos y termina servidor

/* ==================== IMPLEMENTACIÓN DE FUNCIONES ==================== */

/**
 * Crear una nueva sala de chat
 * 
 * Genera una clave única usando ftok() y crea una cola de mensajes System V
 * asociada a la nueva sala. Inicializa la estructura de datos en memoria
 * y registra la creación en los logs del servidor.
 * 
 * @param nombre Nombre de la sala a crear (debe ser único)
 * @return Índice de la sala creada en el array, o -1 si hay error
 */
int crear_sala(const char *nombre) {
    // Verificar que no excedamos el límite máximo de salas
    if (num_salas >= MAX_SALAS) {
        printf("[ERROR] Límite máximo de salas alcanzado (%d)\n", MAX_SALAS);
        return -1;
    }
    
    // Generar clave única para la cola de mensajes de esta sala
    // Usamos proj_id diferente por sala para evitar colisiones
    key_t key = ftok("/tmp", 100 + num_salas);
    if (key == (key_t)-1) { 
        perror("[ERROR] ftok para nueva sala"); 
        return -1; 
    }
    
    // Crear cola de mensajes System V para la sala
    int cola_id = msgget(key, IPC_CREAT | 0666);
    if (cola_id == -1) { 
        perror("[ERROR] msgget para nueva sala"); 
        return -1; 
    }

    // Inicializar estructura de sala en memoria
    strncpy(salas[num_salas].nombre, nombre, MAX_NOMBRE - 1);
    salas[num_salas].nombre[MAX_NOMBRE - 1] = '\0';  // Asegurar terminación nula
    salas[num_salas].cola_id = cola_id;
    salas[num_salas].num_usuarios = 0;
    
    // Log de creación exitosa
    printf("[SERVIDOR] Sala creada: '%s' (ID=%d, Índice=%d)\n", 
           nombre, cola_id, num_salas);
    
    num_salas++;
    return num_salas - 1;
}

/**
 * Buscar una sala por su nombre
 * 
 * Recorre el array de salas activas buscando una que coincida
 * exactamente con el nombre especificado.
 * 
 * @param nombre Nombre de la sala a buscar
 * @return Índice de la sala si existe, -1 si no se encuentra
 */
int buscar_sala(const char *nombre) {
    for (int i = 0; i < num_salas; i++) {
        if (strcmp(salas[i].nombre, nombre) == 0) {
            return i;  // Sala encontrada, retornar índice
        }
    }
    return -1;  // Sala no encontrada
}

/**
 * Agregar un usuario a una sala específica
 * 
 * Registra al usuario en la sala guardando su nombre y el ID de su cola
 * privada para poder enviarle mensajes posteriormente. Verifica duplicados,
 * límites de capacidad y validez de parámetros.
 * 
 * @param indice_sala Índice de la sala en el array de salas
 * @param nombre_usuario Nombre del usuario a agregar
 * @param qid_usuario ID de la cola privada del usuario
 * @return 0 si éxito, -1 si error (sala inválida, llena, o usuario duplicado)
 */
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario) {
    // Validar índice de sala
    if (indice_sala < 0 || indice_sala >= num_salas) {
        printf("[ERROR] Índice de sala inválido: %d\n", indice_sala);
        return -1;
    }
    
    struct sala *s = &salas[indice_sala];
    
    // Verificar capacidad de la sala
    if (s->num_usuarios >= MAX_USUARIOS_POR_SALA) {
        printf("[ERROR] Sala '%s' llena (%d/%d usuarios)\n", 
               s->nombre, s->num_usuarios, MAX_USUARIOS_POR_SALA);
        return -1;
    }
    
    // Verificar que el usuario no esté ya en la sala (evitar duplicados)
    for (int i = 0; i < s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], nombre_usuario) == 0) {
            printf("[WARNING] Usuario '%s' ya está en sala '%s'\n", 
                   nombre_usuario, s->nombre);
            return -1;
        }
    }

    // Agregar usuario a la sala
    strncpy(s->usuarios[s->num_usuarios], nombre_usuario, MAX_NOMBRE - 1);
    s->usuarios[s->num_usuarios][MAX_NOMBRE - 1] = '\0';  // Terminación nula
    s->usuarios_qid[s->num_usuarios] = qid_usuario;
    s->num_usuarios++;
    
    printf("[SERVIDOR] Usuario '%s' agregado a sala '%s' (%d/%d usuarios)\n", 
           nombre_usuario, s->nombre, s->num_usuarios, MAX_USUARIOS_POR_SALA);
    return 0;
}

/**
 * Guardar mensaje en historial persistente de la sala
 * 
 * Crea o añade mensajes a un archivo de texto que actúa como historial
 * persistente de la sala. Cada sala tiene su propio archivo nombrado
 * según el nombre de la sala con extensión .txt.
 * 
 * @param indice_sala Índice de la sala en el array
 * @param msg Mensaje a guardar en el historial
 */
void guardar_historial(int indice_sala, struct mensaje *msg) {
    // Validar parámetros
    if (indice_sala < 0 || indice_sala >= num_salas || !msg) {
        printf("[ERROR] Parámetros inválidos para guardar historial\n");
        return;
    }
    
    // Generar nombre de archivo basado en el nombre de la sala
    char filename[150];
    snprintf(filename, sizeof(filename), "%s.txt", salas[indice_sala].nombre);
    
    // Abrir archivo en modo append (crear si no existe)
    FILE *f = fopen(filename, "a");
    if (!f) { 
        perror("[ERROR] No se pudo abrir archivo de historial"); 
        return; 
    }
    
    // Escribir mensaje con formato: "Usuario: mensaje"
    fprintf(f, "%s: %s\n", msg->remitente, msg->texto);
    fclose(f);
    
    // Log opcional para debugging
    // printf("[DEBUG] Historial guardado en %s\n", filename);
}

/**
 * Distribuir mensaje a todos los usuarios de una sala
 * 
 * Toma un mensaje recibido de un usuario y lo distribuye a todos los demás
 * usuarios de la misma sala usando sus colas privadas. El remitente original
 * no recibe una copia de su propio mensaje. Además, guarda el mensaje en
 * el historial persistente de la sala.
 * 
 * @param indice_sala Índice de la sala donde distribuir el mensaje
 * @param msg Mensaje original recibido del cliente
 */
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg) {
    // Validar parámetros
    if (indice_sala < 0 || indice_sala >= num_salas || !msg) {
        printf("[ERROR] Parámetros inválidos para distribución\n");
        return;
    }
    
    struct sala *s = &salas[indice_sala];
    
    // Log de actividad de distribución
    printf("[DISTRIBUCIÓN] Sala '%s': '%s' dice: %s (enviando a %d usuarios)\n", 
           s->nombre, msg->remitente, msg->texto, s->num_usuarios - 1);

    // Construir mensaje de salida tipo CHAT para distribución
    struct mensaje out;
    out.mtype = 4;  // Tipo CHAT para mensajes distribuidos
    out.reply_qid = 0;  // No necesario para mensajes de difusión
    
    // Copiar datos del mensaje original con terminación nula segura
    strncpy(out.remitente, msg->remitente, MAX_NOMBRE - 1);
    out.remitente[MAX_NOMBRE - 1] = '\0';
    strncpy(out.texto, msg->texto, MAX_TEXTO - 1);
    out.texto[MAX_TEXTO - 1] = '\0';
    strncpy(out.sala, msg->sala, MAX_NOMBRE - 1);
    out.sala[MAX_NOMBRE - 1] = '\0';

    // Distribuir mensaje a todos los usuarios de la sala (excepto remitente)
    for (int i = 0; i < s->num_usuarios; i++) {
        // Excluir al remitente (no enviarse el mensaje a sí mismo)
        if (strcmp(s->usuarios[i], msg->remitente) == 0) {
            continue;
        }

        // Obtener ID de cola privada del usuario destinatario
        int qid_dest = s->usuarios_qid[i];
        
        // Intentar enviar mensaje a la cola del usuario
        if (msgsnd(qid_dest, &out, sizeof(out) - sizeof(long), 0) == -1) {
            // Registrar error pero continuar con otros usuarios
            fprintf(stderr, "[ERROR] No se pudo enviar mensaje a '%s' (qid=%d): %s\n", 
                    s->usuarios[i], qid_dest, strerror(errno));
        }
    }
    
    // Guardar mensaje en historial persistente de la sala
    guardar_historial(indice_sala, msg);
}

/**
 * Función de limpieza y terminación del servidor
 * 
 * Esta función se ejecuta cuando el servidor recibe SIGINT (Ctrl+C) o SIGTERM.
 * Se encarga de eliminar todas las colas de mensajes System V creadas durante
 * la ejecución para evitar que queden recursos huérfanos en el sistema.
 * 
 * @param signo Número de la señal recibida
 */
void limpiar_colas_y_salir(int signo) {
    printf("\n[SERVIDOR] Señal de terminación recibida (%d), iniciando limpieza...\n", signo);
    
    // Eliminar cola global si existe
    if (cola_global != -1) {
        if (msgctl(cola_global, IPC_RMID, NULL) == 0) {
            printf("[LIMPIEZA] Cola global eliminada correctamente\n");
        } else {
            perror("[ERROR] No se pudo eliminar cola global");
        }
    }
    
    // Eliminar todas las colas de salas creadas
    for (int i = 0; i < num_salas; i++) {
        if (salas[i].cola_id != -1) {
            if (msgctl(salas[i].cola_id, IPC_RMID, NULL) == 0) {
                printf("[LIMPIEZA] Cola de sala '%s' eliminada correctamente\n", salas[i].nombre);
            } else {
                fprintf(stderr, "[ERROR] No se pudo eliminar cola de sala '%s': %s\n", 
                        salas[i].nombre, strerror(errno));
            }
        }
    }
    
    printf("[SERVIDOR] Terminado correctamente. Archivos de historial conservados.\n");
    exit(0);
}

/* ==================== FUNCIÓN PRINCIPAL ==================== */

/**
 * Función principal del servidor de chat
 * 
 * Inicializa el servidor, crea la cola global, instala manejadores de señales
 * y entra en el bucle principal de procesamiento de mensajes. Maneja todos
 * los tipos de mensajes del protocolo y coordina las operaciones del sistema.
 */
int main() {
    /* Configuración inicial del servidor */
    
    // Instalar manejadores de señales para limpieza automática
    signal(SIGINT, limpiar_colas_y_salir);   // Ctrl+C
    signal(SIGTERM, limpiar_colas_y_salir);  // Terminación solicitada por el sistema

    /* Crear cola global de comunicación */
    
    // Generar clave conocida para la cola global
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) { 
        perror("[ERROR] No se pudo generar clave para cola global"); 
        exit(1);
    }
    
    // Crear cola global donde llegarán todos los mensajes de clientes
    cola_global = msgget(key_global, IPC_CREAT | 0666);
    if (cola_global == -1) { 
        perror("[ERROR] No se pudo crear cola global"); 
        exit(1);
    }
    
    /* Mostrar información de inicio */
    printf("\n=== SERVIDOR DE CHAT MULTI-SALA ===\n");
    printf("Servidor iniciado correctamente\n");
    printf("Cola global ID: %d\n", cola_global);
    printf("Capacidad: %d salas, %d usuarios por sala\n", MAX_SALAS, MAX_USUARIOS_POR_SALA);
    printf("Esperando conexiones de clientes...\n");
    printf("Presiona Ctrl+C para terminar el servidor\n");
    printf("=====================================\n\n");

    /* Bucle principal de procesamiento de mensajes */
    struct mensaje msg;
    while (1) {
        // Recibir cualquier tipo de mensaje de la cola global
        ssize_t r = msgrcv(cola_global, &msg, sizeof(msg) - sizeof(long), 0, 0);
        
        // Manejar errores de recepción
        if (r == -1) { 
            if (errno == EINTR) {
                // Interrupción por señal, continuar normalmente
                continue;
            }
            perror("[ERROR] Error recibiendo mensaje de cola global"); 
            continue;
        }

        /* ===== PROCESAMIENTO DE MENSAJE JOIN (Tipo 1) ===== */
        if (msg.mtype == 1) {
            printf("[JOIN] Usuario '%s' solicita unirse a sala '%s'\n", 
                   msg.remitente, msg.sala);
            
            // Buscar si la sala ya existe
            int idx = buscar_sala(msg.sala);
            
            // Si no existe, intentar crearla
            if (idx == -1) {
                idx = crear_sala(msg.sala);
            }
            
            if (idx == -1) {
                // Error al crear sala (límite alcanzado)
                struct mensaje resp = {.mtype = 2};
                snprintf(resp.texto, MAX_TEXTO, 
                        "Error: no se pudo crear la sala '%s' (límite de %d salas alcanzado)", 
                        msg.sala, MAX_SALAS);
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
                continue;
            }
            
            // Intentar agregar usuario a la sala
            if (agregar_usuario_a_sala(idx, msg.remitente, msg.reply_qid) != 0) {
                // Error al agregar (duplicado o sala llena)
                struct mensaje resp = {.mtype = 2};
                snprintf(resp.texto, MAX_TEXTO, 
                        "Error: no se pudo agregar a '%s' (usuario duplicado o sala llena)", 
                        msg.remitente);
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
            } else {
                // Éxito al agregar usuario
                struct mensaje resp = {.mtype = 2};
                snprintf(resp.texto, MAX_TEXTO, 
                        "Te has unido exitosamente a la sala: %s", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
            }
        } else if (msg.mtype == 3) {
            /* ===== PROCESAMIENTO DE MENSAJE MSG (Tipo 3) ===== */
            printf("[MSG] Usuario '%s' en sala '%s': %s\n", 
                   msg.remitente, msg.sala, msg.texto);
            
            // Buscar la sala de destino
            int idx = buscar_sala(msg.sala);
            if (idx != -1) {
                // Sala encontrada, distribuir mensaje a todos los usuarios
                enviar_a_todos_en_sala(idx, &msg);
            } else {
                // Sala no existe, notificar error al remitente
                struct mensaje resp = {.mtype = 2};
                snprintf(resp.texto, MAX_TEXTO, 
                        "Error: la sala '%s' no existe o fue eliminada", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
                printf("[ERROR] Usuario '%s' intentó enviar mensaje a sala inexistente '%s'\n", 
                       msg.remitente, msg.sala);
            }
            
        } else if (msg.mtype == 5) {
            /* ===== PROCESAMIENTO DE MENSAJE LEAVE (Tipo 5) ===== */
            printf("[LEAVE] Usuario '%s' abandona sala '%s'\n", 
                   msg.remitente, msg.sala);
            
            // Buscar la sala
            int idx = buscar_sala(msg.sala);
            if (idx != -1) {
                struct sala *s = &salas[idx];
                int found = -1;
                
                // Buscar el usuario en la lista de la sala
                for (int i = 0; i < s->num_usuarios; i++) {
                    if (strcmp(s->usuarios[i], msg.remitente) == 0) { 
                        found = i; 
                        break;
                    }
                }
                
                if (found != -1) {
                    // Remover usuario desplazando el array
                    for (int j = found; j < s->num_usuarios - 1; j++) {
                        strncpy(s->usuarios[j], s->usuarios[j + 1], MAX_NOMBRE);
                        s->usuarios_qid[j] = s->usuarios_qid[j + 1];
                    }
                    s->num_usuarios--;
                    
                    // Confirmar salida al usuario
                    struct mensaje resp = {.mtype = 2};
                    snprintf(resp.texto, MAX_TEXTO, 
                            "Has abandonado la sala: %s", msg.sala);
                    msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
                    
                    printf("[SERVIDOR] Usuario '%s' removído de sala '%s' (%d usuarios restantes)\n", 
                           msg.remitente, msg.sala, s->num_usuarios);
                }
            }
        } else if (msg.mtype == 6) {
            /* ===== PROCESAMIENTO DE MENSAJE USERS (Tipo 6) ===== */
            printf("[USERS] Solicitud de lista de usuarios en sala '%s'\n", msg.sala);
            
            int idx = buscar_sala(msg.sala);
            if (idx != -1) {
                struct sala *s = &salas[idx];
                struct mensaje resp = {.mtype = 2};
                
                // Construir lista de usuarios
                char buf[512] = "Usuarios en sala: ";
                for (int i = 0; i < s->num_usuarios; i++) {
                    strcat(buf, s->usuarios[i]);
                    if (i < s->num_usuarios - 1) {
                        strcat(buf, ", ");
                    }
                }
                
                // Añadir información adicional
                char info[100];
                snprintf(info, sizeof(info), " (%d/%d usuarios)", 
                        s->num_usuarios, MAX_USUARIOS_POR_SALA);
                strcat(buf, info);
                
                strncpy(resp.texto, buf, MAX_TEXTO - 1);
                resp.texto[MAX_TEXTO - 1] = '\0';
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
            } else {
                // Sala no existe
                struct mensaje resp = {.mtype = 2};
                snprintf(resp.texto, MAX_TEXTO, "Error: la sala '%s' no existe", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
            }
            
        } else if (msg.mtype == 7) {
            /* ===== PROCESAMIENTO DE MENSAJE LIST (Tipo 7) ===== */
            printf("[LIST] Solicitud de lista de salas disponibles\n");
            
            struct mensaje resp = {.mtype = 2};
            
            if (num_salas == 0) {
                strcpy(resp.texto, "No hay salas disponibles. ¡Crea la primera con 'join <nombre>!");
            } else {
                char buf[512] = "Salas disponibles: ";
                for (int i = 0; i < num_salas; i++) {
                    strcat(buf, salas[i].nombre);
                    
                    // Añadir contador de usuarios
                    char count[20];
                    snprintf(count, sizeof(count), "(%d)", salas[i].num_usuarios);
                    strcat(buf, count);
                    
                    if (i < num_salas - 1) {
                        strcat(buf, ", ");
                    }
                }
                
                strncpy(resp.texto, buf, MAX_TEXTO - 1);
                resp.texto[MAX_TEXTO - 1] = '\0';
            }
            
            msgsnd(msg.reply_qid, &resp, sizeof(resp) - sizeof(long), 0);
            
        } else {
            /* ===== MENSAJE DE TIPO DESCONOCIDO ===== */
            printf("[WARNING] Mensaje de tipo desconocido recibido: %ld\n", msg.mtype);
            printf("          Remitente: '%s', Sala: '%s', Texto: '%s'\n", 
                   msg.remitente, msg.sala, msg.texto);
        }
    }
    
    // Aca nunca llega... o si?
    return 0;
}
