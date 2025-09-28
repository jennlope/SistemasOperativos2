#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define MAX_TEXTO 256
#define MAX_NOMBRE 50

struct mensaje {
    long mtype;
    int reply_qid;
    char remitente[MAX_NOMBRE];
    char texto[MAX_TEXTO];
    char sala[MAX_NOMBRE];
};

int cola_global = -1;
int cola_privada = -1;
char nombre_usuario[MAX_NOMBRE];
char sala_actual[MAX_NOMBRE] = "";

// Manejo de salida
void limpiar_y_salir(int signo) {
    if (cola_privada != -1) msgctl(cola_privada, IPC_RMID, NULL);
    printf("\nCliente %s: saliendo\n", nombre_usuario);
    exit(0);
}

// Hilo para recibir mensajes
void *recibir_mensajes(void *arg) {
    struct mensaje msg;
    while (1) {
        ssize_t r = msgrcv(cola_privada, &msg, sizeof(msg) - sizeof(long), 0, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv privado");
            continue;
        }

        if (msg.mtype == 2) { 
            printf("[SERVIDOR] %s\n", msg.texto);
        } else if (msg.mtype == 4) { 
            printf("%s: %s\n", msg.remitente, msg.texto);
        } else {
            printf("[TIPO %ld] %s\n", msg.mtype, msg.texto);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <nombre_usuario>\n", argv[0]);
        exit(1);
    }
    signal(SIGINT, limpiar_y_salir);
    strcpy(nombre_usuario, argv[1]);

    // Conectar a cola global
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) { perror("ftok global"); exit(1); }
    cola_global = msgget(key_global, 0666);
    if (cola_global == -1) { perror("msgget global"); exit(1); }

    // Crear cola privada
    cola_privada = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (cola_privada == -1) { perror("msgget privado"); exit(1); }

    printf("Bienvenid@ %s. Salas disponibles: General, Deportes\n", nombre_usuario);

    // Hilo receptor
    pthread_t hilo;
    pthread_create(&hilo, NULL, recibir_mensajes, NULL);

    struct mensaje msg;
    char comando[MAX_TEXTO];

    while (1) {
        printf("> ");
        if (!fgets(comando, sizeof(comando), stdin)) break;
        comando[strcspn(comando, "\n")] = '\0';

        if (strncmp(comando, "join ", 5) == 0) {
            char sala[MAX_NOMBRE];
            sscanf(comando + 5, "%49s", sala);

            memset(&msg,0,sizeof(msg));
            msg.mtype = 1;
            msg.reply_qid = cola_privada;
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE);
            strncpy(msg.sala, sala, MAX_NOMBRE);
            msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long),0);
            strncpy(sala_actual, sala, MAX_NOMBRE);

        } else if (strncmp(comando,"/leave",5)==0) {
            if(strlen(sala_actual)==0) { printf("No estás en ninguna sala\n"); continue; }
            memset(&msg,0,sizeof(msg));
            msg.mtype = 5;
            msg.reply_qid = cola_privada;
            strncpy(msg.remitente,nombre_usuario,MAX_NOMBRE);
            strncpy(msg.sala,sala_actual,MAX_NOMBRE);
            msgsnd(cola_global,&msg,sizeof(msg)-sizeof(long),0);
            sala_actual[0]='\0';

        } else if (strncmp(comando,"/list",4)==0) {
            memset(&msg,0,sizeof(msg));
            msg.mtype = 7;
            msg.reply_qid = cola_privada;
            msgsnd(cola_global,&msg,sizeof(msg)-sizeof(long),0);

        } else if (strncmp(comando,"/users",5)==0) {
            if(strlen(sala_actual)==0) { printf("No estás en ninguna sala\n"); continue; }
            memset(&msg,0,sizeof(msg));
            msg.mtype = 6;
            msg.reply_qid = cola_privada;
            strncpy(msg.sala,sala_actual,MAX_NOMBRE);
            msgsnd(cola_global,&msg,sizeof(msg)-sizeof(long),0);

        } else if(strlen(comando)>0) {
            if(strlen(sala_actual)==0) { printf("No estás en ninguna sala\n"); continue; }
            memset(&msg,0,sizeof(msg));
            msg.mtype=3;
            msg.reply_qid=cola_privada;
            strncpy(msg.remitente,nombre_usuario,MAX_NOMBRE);
            strncpy(msg.sala,sala_actual,MAX_NOMBRE);
            strncpy(msg.texto,comando,MAX_TEXTO);
            msgsnd(cola_global,&msg,sizeof(msg)-sizeof(long),0);
        }
    }

    limpiar_y_salir(0);
    return 0;
}
