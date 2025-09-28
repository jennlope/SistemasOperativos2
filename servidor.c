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
    long mtype;
    int reply_qid;
    char remitente[MAX_NOMBRE];
    char texto[MAX_TEXTO];
    char sala[MAX_NOMBRE];
};

struct sala {
    char nombre[MAX_NOMBRE];
    int cola_id;
    int num_usuarios;
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];
    int usuarios_qid[MAX_USUARIOS_POR_SALA];
};

struct sala salas[MAX_SALAS];
int num_salas = 0;
int cola_global = -1;

// --- Prototipos ---
int crear_sala(const char *nombre);
int buscar_sala(const char *nombre);
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario);
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg);
void guardar_historial(int indice_sala, struct mensaje *msg);
void limpiar_colas_y_salir(int signo);

// --- Funciones ---
int crear_sala(const char *nombre) {
    if(num_salas >= MAX_SALAS) return -1;
    key_t key = ftok("/tmp", 100+num_salas);
    if(key == (key_t)-1) { perror("ftok"); return -1; }
    int cola_id = msgget(key, IPC_CREAT | 0666);
    if(cola_id==-1) { perror("msgget"); return -1; }

    strncpy(salas[num_salas].nombre,nombre,MAX_NOMBRE);
    salas[num_salas].cola_id=cola_id;
    salas[num_salas].num_usuarios=0;
    printf("[SERVIDOR] Sala creada: %s (cola_id=%d)\n", nombre, cola_id);
    num_salas++;
    return num_salas-1;
}

int buscar_sala(const char *nombre) {
    for(int i=0;i<num_salas;i++)
        if(strcmp(salas[i].nombre,nombre)==0) return i;
    return -1;
}

int agregar_usuario_a_sala(int indice_sala,const char *nombre_usuario,int qid_usuario) {
    if(indice_sala<0 || indice_sala>=num_salas) return -1;
    struct sala *s=&salas[indice_sala];
    if(s->num_usuarios>=MAX_USUARIOS_POR_SALA) return -1;
    for(int i=0;i<s->num_usuarios;i++)
        if(strcmp(s->usuarios[i],nombre_usuario)==0) return -1;

    strncpy(s->usuarios[s->num_usuarios],nombre_usuario,MAX_NOMBRE);
    s->usuarios_qid[s->num_usuarios]=qid_usuario;
    s->num_usuarios++;
    printf("[SERVIDOR] Usuario %s agregado a la sala %s\n", nombre_usuario, s->nombre);
    return 0;
}

void guardar_historial(int indice_sala, struct mensaje *msg) {
    if(indice_sala<0 || indice_sala>=num_salas) return;
    char filename[100];
    snprintf(filename,sizeof(filename),"%s.txt",salas[indice_sala].nombre);
    FILE *f=fopen(filename,"a");
    if(!f){ perror("fopen historial"); return; }
    fprintf(f,"%s: %s\n", msg->remitente, msg->texto);
    fclose(f);
}

void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg) {
    if(indice_sala<0 || indice_sala>=num_salas) return;
    struct sala *s=&salas[indice_sala];

    struct mensaje out;
    out.mtype=4;
    strncpy(out.remitente,msg->remitente,MAX_NOMBRE);
    strncpy(out.texto,msg->texto,MAX_TEXTO);
    strncpy(out.sala,msg->sala,MAX_NOMBRE);

    for(int i=0;i<s->num_usuarios;i++){
        if(strcmp(s->usuarios[i],msg->remitente)==0) continue;
        int qid_dest=s->usuarios_qid[i];
        if(msgsnd(qid_dest,&out,sizeof(out)-sizeof(long),0)==-1){
            fprintf(stderr,"[SERVIDOR] Error al enviar a %s (qid=%d): %s\n",s->usuarios[i],qid_dest,strerror(errno));
        }
    }
    guardar_historial(indice_sala,msg);
}

void limpiar_colas_y_salir(int signo){
    printf("\n[SERVIDOR] Limpiando colas...\n");
    if(cola_global!=-1) { msgctl(cola_global,IPC_RMID,NULL); printf("Cola global eliminada\n"); }
    for(int i=0;i<num_salas;i++)
        if(salas[i].cola_id!=-1){ msgctl(salas[i].cola_id,IPC_RMID,NULL); printf("Cola de sala '%s' eliminada\n",salas[i].nombre);}
    exit(0);
}

// --- MAIN ---
int main(){
    signal(SIGINT,limpiar_colas_y_salir);
    signal(SIGTERM,limpiar_colas_y_salir);

    key_t key_global=ftok("/tmp",'A');
    if(key_global==(key_t)-1){ perror("ftok global"); exit(1);}
    cola_global=msgget(key_global,IPC_CREAT|0666);
    if(cola_global==-1){ perror("msgget global"); exit(1);}
    printf("[SERVIDOR] Iniciado. Esperando clientes...\n");

    struct mensaje msg;
    while(1){
        ssize_t r=msgrcv(cola_global,&msg,sizeof(msg)-sizeof(long),0,0);
        if(r==-1){ if(errno==EINTR) continue; perror("msgrcv global"); continue;}

        if(msg.mtype==1){ // JOIN
            int idx=buscar_sala(msg.sala);
            if(idx==-1) idx=crear_sala(msg.sala);
            if(idx==-1){
                struct mensaje resp={.mtype=2};
                snprintf(resp.texto,MAX_TEXTO,"Error: no se pudo crear sala %s",msg.sala);
                msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
                continue;
            }
            if(agregar_usuario_a_sala(idx,msg.remitente,msg.reply_qid)!=0){
                struct mensaje resp={.mtype=2};
                snprintf(resp.texto,MAX_TEXTO,"Error: no se pudo agregar a %s (duplicado o sala llena)",msg.remitente);
                msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
            } else {
                struct mensaje resp={.mtype=2};
                snprintf(resp.texto,MAX_TEXTO,"Te has unido a la sala: %s",msg.sala);
                msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
            }
        } else if(msg.mtype==3){ // MSG
            int idx=buscar_sala(msg.sala);
            if(idx!=-1) enviar_a_todos_en_sala(idx,&msg);
            else {
                struct mensaje resp={.mtype=2};
                snprintf(resp.texto,MAX_TEXTO,"Error: la sala %s no existe",msg.sala);
                msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
            }
        } else if(msg.mtype==5){ // LEAVE
            int idx=buscar_sala(msg.sala);
            if(idx!=-1){
                struct sala *s=&salas[idx];
                int found=-1;
                for(int i=0;i<s->num_usuarios;i++)
                    if(strcmp(s->usuarios[i],msg.remitente)==0){ found=i; break;}
                if(found!=-1){
                    for(int j=found;j<s->num_usuarios-1;j++){
                        strncpy(s->usuarios[j],s->usuarios[j+1],MAX_NOMBRE);
                        s->usuarios_qid[j]=s->usuarios_qid[j+1];
                    }
                    s->num_usuarios--;
                    struct mensaje resp={.mtype=2};
                    snprintf(resp.texto,MAX_TEXTO,"Has abandonado la sala: %s",msg.sala);
                    msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
                    printf("[SERVIDOR] Usuario %s abandonó la sala %s\n",msg.remitente,msg.sala);
                }
            }
        } else if(msg.mtype==6){ // USERS
            int idx=buscar_sala(msg.sala);
            if(idx!=-1){
                struct sala *s=&salas[idx];
                struct mensaje resp={.mtype=2};
                char buf[256]="Usuarios: ";
                for(int i=0;i<s->num_usuarios;i++){
                    strcat(buf,s->usuarios[i]);
                    if(i<s->num_usuarios-1) strcat(buf,", ");
                }
                strncpy(resp.texto,buf,MAX_TEXTO);
                msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
            }
        } else if(msg.mtype==7){ // LIST
            struct mensaje resp={.mtype=2};
            char buf[256]="Salas: ";
            for(int i=0;i<num_salas;i++){
                strcat(buf,salas[i].nombre);
                if(i<num_salas-1) strcat(buf,", ");
            }
            strncpy(resp.texto,buf,MAX_TEXTO);
            msgsnd(msg.reply_qid,&resp,sizeof(resp)-sizeof(long),0);
        } else {
            printf("[SERVIDOR] Tipo desconocido: %ld\n",msg.mtype);
        }
    }
    return 0;
}
