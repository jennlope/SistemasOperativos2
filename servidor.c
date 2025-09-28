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

struct mensaje {
    long mtype;                     // tipo de mensaje
    int reply_qid;                  // para requests: cola privada del cliente (JOIN)
    char remitente[MAX_NOMBRE];
    char texto[MAX_TEXTO];
    char sala[MAX_NOMBRE];
};

struct sala {
    char nombre[MAX_NOMBRE];
    int cola_id;                    // cola asociada a la sala (se crea para cumplir el requisito)
    int num_usuarios;
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];
    int usuarios_qid[MAX_USUARIOS_POR_SALA]; // cola privada de cada usuario
};

struct sala salas[MAX_SALAS];
int num_salas = 0;

int cola_global = -1;

// Prototipos
int crear_sala(const char *nombre);
int buscar_sala(const char *nombre);
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario);
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg);
void limpiar_colas_y_salir(int signo);

int crear_sala(const char *nombre) {
    if (num_salas >= MAX_SALAS) {
        return -1;
    }

    // generar clave única usando ftok con diferente proj_id
    key_t key = ftok("/tmp", 100 + num_salas); // 100+ índice para variar
    if (key == (key_t)-1) {
        perror("ftok");
        return -1;
    }
    int cola_id = msgget(key, IPC_CREAT | 0666);
    if (cola_id == -1) {
        perror("msgget crear sala");
        return -1;
    }

    strncpy(salas[num_salas].nombre, nombre, MAX_NOMBRE);
    salas[num_salas].cola_id = cola_id;
    salas[num_salas].num_usuarios = 0;
    num_salas++;
    return num_salas - 1;
}

int buscar_sala(const char *nombre) {
    for (int i = 0; i < num_salas; i++) {
        if (strcmp(salas[i].nombre, nombre) == 0) return i;
    }
    return -1;
}

int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario) {
    if (indice_sala < 0 || indice_sala >= num_salas) return -1;
    struct sala *s = &salas[indice_sala];
    if (s->num_usuarios >= MAX_USUARIOS_POR_SALA) return -1;

    for (int i = 0; i < s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], nombre_usuario) == 0) {
            return -1; // ya existe
        }
    }

    strncpy(s->usuarios[s->num_usuarios], nombre_usuario, MAX_NOMBRE);
    s->usuarios_qid[s->num_usuarios] = qid_usuario;
    s->num_usuarios++;
    return 0;
}

void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg) {
    if (indice_sala < 0 || indice_sala >= num_salas) return;
    struct sala *s = &salas[indice_sala];

    // Construir mensaje de tipo CHAT para clientes
    struct mensaje out;
    out.mtype = 4; // CHAT
    out.reply_qid = 0;
    strncpy(out.remitente, msg->remitente, MAX_NOMBRE);
    strncpy(out.texto, msg->texto, MAX_TEXTO);
    strncpy(out.sala, msg->sala, MAX_NOMBRE);

    for (int i = 0; i < s->num_usuarios; i++) {
        // no reenviar al remitente
        if (strcmp(s->usuarios[i], msg->remitente) == 0) continue;

        int qid_dest = s->usuarios_qid[i];
        if (msgsnd(qid_dest, &out, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
            // si falla, mostramos pero continuamos
            fprintf(stderr, "Error al enviar a %s (qid=%d): %s\n", s->usuarios[i], qid_dest, strerror(errno));
        }
    }
}

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
    signal(SIGINT, limpiar_colas_y_salir);
    signal(SIGTERM, limpiar_colas_y_salir);

    // crear cola global
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
        // recibir de cualquier tipo
        ssize_t r = msgrcv(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv global");
            continue;
        }

        if (msg.mtype == 1) { // JOIN
            printf("JOIN: usuario='%s' quiere unirse a sala='%s' (qid_cliente=%d)\n", msg.remitente, msg.sala, msg.reply_qid);
            int idx = buscar_sala(msg.sala);
            if (idx == -1) {
                idx = crear_sala(msg.sala);
                if (idx == -1) {
                    // enviar error al cliente
                    struct mensaje resp;
                    resp.mtype = 2;
                    resp.reply_qid = 0;
                    snprintf(resp.texto, MAX_TEXTO, "Error: no se pudo crear la sala %s", msg.sala);
                    msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
                    continue;
                }
                printf("Nueva sala creada: %s (cola_id=%d)\n", msg.sala, salas[idx].cola_id);
            }

            if (agregar_usuario_a_sala(idx, msg.remitente, msg.reply_qid) == 0) {
                printf("Usuario %s agregado a la sala %s\n", msg.remitente, msg.sala);
                // enviar confirmación al cliente
                struct mensaje resp;
                resp.mtype = 2; // RESP
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Te has unido a la sala: %s", msg.sala);
                // También podemos enviar la cola de la sala en texto si se desea
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            } else {
                struct mensaje resp;
                resp.mtype = 2;
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Error: no se pudo agregar a %s a la sala %s (posible duplicado o sala llena)", msg.remitente, msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
        } else if (msg.mtype == 3) { // MSG enviado por cliente
            printf("MSG en sala='%s' de '%s': %s\n", msg.sala, msg.remitente, msg.texto);
            int idx = buscar_sala(msg.sala);
            if (idx != -1) {
                enviar_a_todos_en_sala(idx, &msg);
            } else {
                // notificar al remitente que la sala no existe
                struct mensaje resp;
                resp.mtype = 2;
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Error: la sala %s no existe", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
        } else {
            printf("Mensaje de tipo desconocido: %ld\n", msg.mtype);
        }
    }

    return 0;
}
