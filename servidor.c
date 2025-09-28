// servidor.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define MAX_SALAS 10
#define MAX_USUARIOS_POR_SALA 20
#define MAX_TEXTO 256
#define MAX_NOMBRE 50

// Mensaje compartido con clientes.
// reply_qid permite al servidor responder a la cola privada del cliente.
struct mensaje {
    long mtype;                     // tipo de mensaje (1 JOIN, 2 RESP, 3 MSG, 4 CHAT, etc.)
    int reply_qid;                  // cola privada del cliente (para respuestas)
    char remitente[MAX_NOMBRE];
    char texto[MAX_TEXTO];
    char sala[MAX_NOMBRE];
};

// Estructura que representa una sala en memoria en el servidor.
struct sala {
    char nombre[MAX_NOMBRE];
    int cola_id;                    // cola asociada a la sala (se crea pero no se usa para broadcast en esta implementación)
    int num_usuarios;
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];
    int usuarios_qid[MAX_USUARIOS_POR_SALA]; // qid de la cola privada de cada usuario
};

struct sala salas[MAX_SALAS];
int num_salas = 0;

int cola_global = -1; // cola global que escucha el servidor

// Prototipos
int crear_sala(const char *nombre);
int buscar_sala(const char *nombre);
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario);
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg);
void limpiar_colas_y_salir(int signo);

// Crear una sala: genera una clave ftok distinta por sala y crea la cola.
// Nota: la cola de sala se crea para cumplir con el requisito, pero la entrega
// real a clientes se realiza en sus colas privadas.
int crear_sala(const char *nombre) {
    if (num_salas >= MAX_SALAS) {
        return -1;
    }

    // usar un proj_id distinto por sala para evitar colisiones ftok
    key_t key = ftok("/tmp", 100 + num_salas);
    if (key == (key_t)-1) {
        perror("ftok");
        return -1;
    }
    int cola_id = msgget(key, IPC_CREAT | 0666);
    if (cola_id == -1) {
        perror("msgget crear sala");
        return -1;
    }

    // inicializar la estructura de sala en memoria
    strncpy(salas[num_salas].nombre, nombre, MAX_NOMBRE);
    salas[num_salas].cola_id = cola_id;
    salas[num_salas].num_usuarios = 0;
    num_salas++;
    return num_salas - 1;
}

// Buscar sala por nombre; devuelve índice o -1 si no existe.
int buscar_sala(const char *nombre) {
    for (int i = 0; i < num_salas; i++) {
        if (strcmp(salas[i].nombre, nombre) == 0) return i;
    }
    return -1;
}

// Agregar usuario a la sala: guarda nombre y qid de su cola privada.
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario) {
    if (indice_sala < 0 || indice_sala >= num_salas) return -1;
    struct sala *s = &salas[indice_sala];
    if (s->num_usuarios >= MAX_USUARIOS_POR_SALA) return -1;

    // evitar duplicados
    for (int i = 0; i < s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], nombre_usuario) == 0) {
            return -1; // ya existe
        }
    }

    strncpy(s->usuarios[s->num_usuarios], nombre_usuario, MAX_NOMBRE);
    s->usuarios_qid[s->num_usuarios] = qid_usuario; // guardar qid para mensajes dirigidos
    s->num_usuarios++;
    return 0;
}

// Reenviar mensaje a todos los usuarios de la sala usando sus colas privadas.
// Excluye al remitente.
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg) {
    if (indice_sala < 0 || indice_sala >= num_salas) return;
    struct sala *s = &salas[indice_sala];

    // construir mensaje de salida tipo CHAT (4)
    struct mensaje out;
    out.mtype = 4; // CHAT
    out.reply_qid = 0; // no necesario para CHAT
    strncpy(out.remitente, msg->remitente, MAX_NOMBRE);
    strncpy(out.texto, msg->texto, MAX_TEXTO);
    strncpy(out.sala, msg->sala, MAX_NOMBRE);

    for (int i = 0; i < s->num_usuarios; i++) {
        // no reenviar al remitente
        if (strcmp(s->usuarios[i], msg->remitente) == 0) continue;

        int qid_dest = s->usuarios_qid[i];
        if (msgsnd(qid_dest, &out, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
            // registrar error pero continuar con los demás usuarios
            fprintf(stderr, "Error al enviar a %s (qid=%d): %s\n", s->usuarios[i], qid_dest, strerror(errno));
        }
    }
}

// Limpieza al recibir SIGINT/SIGTERM: eliminar colas System V creadas.
void limpiar_colas_y_salir(int signo) {
    printf("\nServidor: limpiando colas...\n");
    if (cola_global != -1) {
        msgctl(cola_global, IPC_RMID, NULL);
        printf("Cola global eliminada\n");
    }
    for (int i = 0; i < num_salas; i++) {
        if (salas[i].cola_id != -1) {
            msgctl(salas[i].cola_id, IPC_RMID, NULL);
            printf("Cola de sala '%s' eliminada\n", salas[i].nombre);
        }
    }
    exit(0);
}

int main() {
    // Instalar manejadores de señal para limpieza
    signal(SIGINT, limpiar_colas_y_salir);
    signal(SIGTERM, limpiar_colas_y_salir);

    // Crear (o conectar si ya existe) la cola global con ftok("/tmp", 'A')
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) {
        perror("ftok global");
        exit(1);
    }
    cola_global = msgget(key_global, IPC_CREAT | 0666);
    if (cola_global == -1) {
        perror("msgget global");
        exit(1);
    }

    printf("Servidor de chat iniciado. Esperando clientes...\n");

    struct mensaje msg;
    while (1) {
        // Recibir cualquier mensaje enviado a la cola global (JOIN, MSG, etc.)
        ssize_t r = msgrcv(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) {
            if (errno == EINTR) continue; // reintentar si fue interrumpido por señal
            perror("msgrcv global");
            continue;
        }

        if (msg.mtype == 1) { // JOIN: cliente quiere unirse a una sala
            printf("JOIN: usuario='%s' quiere unirse a sala='%s' (qid_cliente=%d)\n", msg.remitente, msg.sala, msg.reply_qid);
            int idx = buscar_sala(msg.sala);
            if (idx == -1) {
                // crear sala si no existe
                idx = crear_sala(msg.sala);
                if (idx == -1) {
                    // enviar error al cliente en su cola privada (usando reply_qid)
                    struct mensaje resp;
                    resp.mtype = 2;
                    resp.reply_qid = 0;
                    snprintf(resp.texto, MAX_TEXTO, "Error: no se pudo crear la sala %s", msg.sala);
                    msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
                    continue;
                }
                printf("Nueva sala creada: %s (cola_id=%d)\n", msg.sala, salas[idx].cola_id);
            }

            // intentar agregar usuario a la sala (guardando su qid)
            if (agregar_usuario_a_sala(idx, msg.remitente, msg.reply_qid) == 0) {
                printf("Usuario %s agregado a la sala %s\n", msg.remitente, msg.sala);
                // enviar confirmación al cliente directamente a su cola privada
                struct mensaje resp;
                resp.mtype = 2; // RESP
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Te has unido a la sala: %s", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            } else {
                // error al agregar (ya existe o sala llena)
                struct mensaje resp;
                resp.mtype = 2;
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Error: no se pudo agregar a %s a la sala %s (posible duplicado o sala llena)", msg.remitente, msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
        } else if (msg.mtype == 3) { // MSG: mensaje enviado por cliente a su sala
            printf("MSG en sala='%s' de '%s': %s\n", msg.sala, msg.remitente, msg.texto);
            int idx = buscar_sala(msg.sala);
            if (idx != -1) {
                // reenviar a todos los usuarios de la sala mediante sus colas privadas
                enviar_a_todos_en_sala(idx, &msg);
            } else {
                // notificar al remitente que la sala no existe (por su cola privada)
                struct mensaje resp;
                resp.mtype = 2;
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Error: la sala %s no existe", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
        } else {
            // mensajes desconocidos (posible extensión futura)
            printf("Mensaje de tipo desconocido: %ld\n", msg.mtype);
        }
    }

    return 0;
}
