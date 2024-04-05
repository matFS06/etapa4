#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h>

#define TAMANHO_FILA 10

typedef struct RelogioProcesso
{
    int tempo[3];
} RelogioProcesso;

typedef struct Mensagem
{
    RelogioProcesso relogio;
    int destino;
    int origem;
} Mensagem;

typedef struct Snapshot
{
    int marcador;
    RelogioProcesso relogio;
} Snapshot;

RelogioProcesso relogioGlobal = {{0, 0, 0}};
Snapshot snapshotP0, snapshotP1, snapshotP2;

int tamanhoFilaEntrada = 0;
pthread_cond_t condFilaCheiaEntrada;
pthread_cond_t condFilaVaziaEntrada;
pthread_mutex_t mutexFilaEntrada;
RelogioProcesso filaEntrada[TAMANHO_FILA];

int tamanhoFilaSaida = 0;
pthread_cond_t condFilaCheiaSaida;
pthread_cond_t condFilaVaziaSaida;
pthread_mutex_t mutexFilaSaida;
Mensagem filaSaida[TAMANHO_FILA];

void imprimirRelogio(RelogioProcesso *relogio, int processo)
{
    printf("Processo: %d, Relogio: (%d, %d, %d)\n", processo, relogio->tempo[0], relogio->tempo[1], relogio->tempo[2]);
}

void imprimirSnapshot(Snapshot *snapshot, int processo)
{
    printf("Snapshot capturado por P%d: Marcador: %d, Relogio: (%d, %d, %d)\n", processo, snapshot->marcador, snapshot->relogio.tempo[0], snapshot->relogio.tempo[1], snapshot->relogio.tempo[2]);
}


void eventoInterno(int pid, RelogioProcesso *relogio)
{
    relogio->tempo[pid]++;
}

void enviarMensagem(int remetente, int destinatario)
{
    pthread_mutex_lock(&mutexFilaSaida);
    relogioGlobal.tempo[remetente]++;
    imprimirRelogio(&relogioGlobal, remetente);

    while (tamanhoFilaSaida == TAMANHO_FILA)
    {
        pthread_cond_wait(&condFilaCheiaSaida, &mutexFilaSaida);
    }

    Mensagem *msg = (Mensagem *)malloc(sizeof(Mensagem));
    
    msg->relogio = relogioGlobal;
    msg->origem = remetente;
    msg->destino = destinatario;

    filaSaida[tamanhoFilaSaida] = *msg;
    tamanhoFilaSaida++;

    pthread_mutex_unlock(&mutexFilaSaida);
    pthread_cond_signal(&condFilaVaziaSaida);
}

void enviarMensagemSaida()
{
    pthread_mutex_lock(&mutexFilaSaida);

    while (tamanhoFilaSaida == 0)
    {
        pthread_cond_wait(&condFilaVaziaSaida, &mutexFilaSaida);
    }

    Mensagem msg = filaSaida[0];
    for (int i = 0; i < tamanhoFilaSaida - 1; i++)
    {
        filaSaida[i] = filaSaida[i + 1];
    }
    tamanhoFilaSaida--;

    int *valoresRelogio;
    valoresRelogio = calloc(3, sizeof(int));

    for (int i = 0; i < 3; i++)
    {
        valoresRelogio[i] = msg.relogio.tempo[i];
    }

    MPI_Send(valoresRelogio, 3, MPI_INT, msg.destino, msg.origem, MPI_COMM_WORLD);

    free(valoresRelogio);

    pthread_mutex_unlock(&mutexFilaSaida);
    pthread_cond_signal(&condFilaCheiaSaida);
}

void receberMensagemEntrada()
{
    int *valoresRelogio;
    valoresRelogio = calloc(3, sizeof(int));

    RelogioProcesso *relogio = (RelogioProcesso *)malloc(sizeof(RelogioProcesso));
    MPI_Recv(valoresRelogio, 3, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    for (int i = 0; i < 3; i++)
    {
        relogio->tempo[i] = valoresRelogio[i];
    }
    free(valoresRelogio);

    pthread_mutex_lock(&mutexFilaEntrada);

    while (tamanhoFilaEntrada == TAMANHO_FILA)
    {
        pthread_cond_wait(&condFilaCheiaEntrada, &mutexFilaEntrada);
    }

    filaEntrada[tamanhoFilaEntrada] = *relogio;
    tamanhoFilaEntrada++;

    pthread_mutex_unlock(&mutexFilaEntrada);
    pthread_cond_signal(&condFilaVaziaEntrada);
}

void receberMensagem(int processo)
{
    pthread_mutex_lock(&mutexFilaEntrada);
    relogioGlobal.tempo[processo]++;

    while (tamanhoFilaEntrada == 0)
    {
        pthread_cond_wait(&condFilaVaziaEntrada, &mutexFilaEntrada);
    }

    RelogioProcesso relogio = filaEntrada[0];
    for (int i = 0; i < tamanhoFilaEntrada - 1; i++)
    {
        filaEntrada[i] = filaEntrada[i + 1];
    }
    tamanhoFilaEntrada--;

    for (int i = 0; i < 3; i++)
    {
        if (relogio.tempo[i] > relogioGlobal.tempo[i])
        {
            relogioGlobal.tempo[i] = relogio.tempo[i];
        }
    }

    imprimirRelogio(&relogioGlobal, processo);

    pthread_mutex_unlock(&mutexFilaEntrada);
    pthread_cond_signal(&condFilaCheiaEntrada);

    // Se o marcador for definido para 1, imprime o snapshot capturado por P0, P1 e P2
    if (snapshotP0.marcador == 1 && processo == 0)
    {
        imprimirSnapshot(&snapshotP0, processo);
        snapshotP0.marcador = 0; // redefine o marcador para evitar imprimir novamente
    }
    else if (snapshotP1.marcador == 1 && processo == 1)
    {
        imprimirSnapshot(&snapshotP1, processo);
        snapshotP1.marcador = 0; // redefine o marcador para evitar imprimir novamente
    }
    else if (snapshotP2.marcador == 1 && processo == 2)
    {
        imprimirSnapshot(&snapshotP2, processo);
        snapshotP2.marcador = 0; // redefine o marcador para evitar imprimir novamente
    }
}

void *threadRelogioProcesso(void *arg)
{
    long idProcesso = (long)arg;
    if (idProcesso == 0)
    {
        eventoInterno(0, &relogioGlobal);
        imprimirRelogio(&relogioGlobal, 0);
        
        // Captura o snapshot em P0 após o primeiro evento externo
        snapshotP0.marcador = 1;
        for (int i = 0; i < 3; i++)
        {
            snapshotP0.relogio.tempo[i] = relogioGlobal.tempo[i];
        }

        enviarMensagem(0, 1);

        receberMensagem(0);

        enviarMensagem(0, 2);

        receberMensagem(0);

        enviarMensagem(0, 1);

        eventoInterno(0, &relogioGlobal);
        imprimirRelogio(&relogioGlobal, 0);
    }

    if (idProcesso == 1)
    {
        enviarMensagem(1, 0);

        // Captura o snapshot em P1 após enviar a mensagem para P0
        snapshotP1.marcador = 1;
        for (int i = 0; i < 3; i++)
        {
            snapshotP1.relogio.tempo[i] = relogioGlobal.tempo[i];
        }

        receberMensagem(1);

        receberMensagem(1);
    }

    if (idProcesso == 2)
    {
        eventoInterno(2, &relogioGlobal);
        imprimirRelogio(&relogioGlobal, 2);

        enviarMensagem(2, 0);

        // Captura o snapshot em P2 após enviar a mensagem para P0
        snapshotP2.marcador = 1;
        for (int i = 0; i < 3; i++)
        {
            snapshotP2.relogio.tempo[i] = relogioGlobal.tempo[i];
        }

        receberMensagem(2);
    }
    return NULL;
}

void *threadEnvioMensagem(void *arg)
{
    long idProcesso = (long)arg;
    while (1)
    {
        enviarMensagemSaida();
    }
    return NULL;
}

void *threadRecebimentoMensagem(void *arg)
{
    long idProcesso = (long)arg;
    while (1)
    {
        receberMensagemEntrada();
    }
    return NULL;
}

void processo(long idProcesso)
{
    pthread_t tEnvioMensagem;
    pthread_t tRecebimentoMensagem;
    pthread_t tRelogioProcesso;

    pthread_cond_init(&condFilaCheiaEntrada, NULL);
    pthread_cond_init(&condFilaVaziaEntrada, NULL);
    pthread_cond_init(&condFilaCheiaSaida, NULL);
    pthread_cond_init(&condFilaVaziaSaida, NULL);
    pthread_mutex_init(&mutexFilaEntrada, NULL);
    pthread_mutex_init(&mutexFilaSaida, NULL);

    pthread_create(&tRelogioProcesso, NULL, &threadRelogioProcesso, (void *)idProcesso);
    pthread_create(&tRecebimentoMensagem, NULL, &threadRecebimentoMensagem, (void *)idProcesso);
    pthread_create(&tEnvioMensagem, NULL, &threadEnvioMensagem, (void *)idProcesso);

    pthread_join(tRelogioProcesso, NULL);
    pthread_join(tRecebimentoMensagem, NULL);
    pthread_join(tEnvioMensagem, NULL);

    pthread_cond_destroy(&condFilaCheiaEntrada);
    pthread_cond_destroy(&condFilaVaziaEntrada);
    pthread_cond_destroy(&condFilaCheiaSaida);
    pthread_cond_destroy(&condFilaVaziaSaida);
    pthread_mutex_destroy(&mutexFilaEntrada);
    pthread_mutex_destroy(&mutexFilaSaida);

    // Se o marcador for definido para 1, imprime o snapshot capturado por P0, P1 e P2
    if (snapshotP0.marcador == 1 && idProcesso == 0)
    {
        imprimirSnapshot(&snapshotP0, idProcesso);
        snapshotP0.marcador = 0; // redefine o marcador para evitar imprimir novamente
    }
}

int main(void)
{
    int meu_rank;

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &meu_rank);

    if (meu_rank == 0)
    {
        processo(0);
    }
    else if (meu_rank == 1)
    {
        processo(1);
    }
    else if (meu_rank == 2)
    {
        processo(2);
    }

    /* Finaliza MPI */
    MPI_Finalize();

    return 0;
}