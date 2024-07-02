#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ncurses.h>

#define MAX_COZINHEIROS 3
#define NUM_BANCADAS 2

//Alunos: Petrus - 2211132, João Távora - 2211220, Kelvin Pimentel - 2211124

typedef struct Pedido {
    char nome[50];
    int tempo_preparo;
    int tempo_cozinha;
    int atendido;
    struct Pedido* proximo;
} Pedido;

typedef struct {
    Pedido* cabeca;
    Pedido* cauda;
    int num_pedidos;
    pthread_mutex_t lock;
} MuralDePedidos;

typedef struct {
    int id;
    int ocupado;
    Pedido* pedido_atual;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} Cozinheiro;

typedef struct {
    int id;
    int ocupado;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} Bancada;

MuralDePedidos mural;
Cozinheiro cozinheiros[MAX_COZINHEIROS];
Bancada bancadas[NUM_BANCADAS];
pthread_mutex_t lock_bancada;
pthread_mutex_t lock_cozinha;
pthread_cond_t cond_bancada;
pthread_cond_t cond_cozinha;
pthread_t thread_pedidos, thread_informacoes, thread_cozinheiros[MAX_COZINHEIROS], thread_gerente;
int tempo_jogo;
int max_tempo_jogo = 60;

void inicializar_mural(MuralDePedidos* mural) {
    mural->cabeca = NULL;
    mural->cauda = NULL;
    mural->num_pedidos = 0;
    pthread_mutex_init(&mural->lock, NULL);
}

void adicionar_pedido(MuralDePedidos* mural, char* nome, int tempo_preparo, int tempo_cozinha) {
    Pedido* novo_pedido = (Pedido*)malloc(sizeof(Pedido));
    if (novo_pedido == NULL) {
        perror("Falha ao alocar memória para novo pedido");
        exit(EXIT_FAILURE);
    }
    strcpy(novo_pedido->nome, nome);
    novo_pedido->tempo_preparo = tempo_preparo;
    novo_pedido->tempo_cozinha = tempo_cozinha;
    novo_pedido->atendido = 0;
    novo_pedido->proximo = NULL;

    pthread_mutex_lock(&mural->lock);
    if (mural->cauda == NULL) {
        mural->cabeca = novo_pedido;
        mural->cauda = novo_pedido;
    } else {
        mural->cauda->proximo = novo_pedido;
        mural->cauda = novo_pedido;
    }
    mural->num_pedidos++;
    pthread_mutex_unlock(&mural->lock);
}

Pedido* obter_proximo_pedido(MuralDePedidos* mural) {
    pthread_mutex_lock(&mural->lock);
    Pedido* pedido = mural->cabeca;
    while (pedido != NULL && pedido->atendido) {
        pedido = pedido->proximo;
    }
    if (pedido != NULL) {
        pedido->atendido = 1;
    }
    pthread_mutex_unlock(&mural->lock);
    return pedido;
}

void* mural_pedidos(void* arg) {
    MuralDePedidos* mural = (MuralDePedidos*)arg;
    char* pedidos[] = {"Hamburguer", "Suco", "Pizza", "Salada", "Sopa", "Sanduiche", "Tacos", "Sorvete", "Bolo", "Cafe"};
    int tempos_preparo[] = {5, 2, 6, 3, 4, 5, 4, 2, 7, 1};
    int tempos_cozinha[] = {3, 1, 5, 2, 3, 4, 3, 1, 6, 1};
    int idx = 0;

    while (1) {
        adicionar_pedido(mural, pedidos[idx], tempos_preparo[idx], tempos_cozinha[idx]);
        printf("Novo pedido adicionado: %s\n", pedidos[idx]);
        idx = (idx + 1) % 10;
        sleep(3); // Adicionar um novo pedido a cada 3 segundos
    }
    return NULL;
}

void* exibir_informacoes(void* arg) {
    MuralDePedidos* mural = (MuralDePedidos*)arg;
    initscr();
    noecho();
    cbreak();
    timeout(1000);

    while (1) {
        clear();
        mvprintw(0, 0, "Tempo de jogo: %d/%d segundos", tempo_jogo, max_tempo_jogo);

        pthread_mutex_lock(&mural->lock);
        mvprintw(2, 0, "Pedidos pendentes: %d", mural->num_pedidos);
        Pedido* pedido = mural->cabeca;
        int contador = 1;
        while (pedido != NULL) {
            mvprintw(3 + contador, 0, "Pedido %d: %s", contador, pedido->nome);
            pedido = pedido->proximo;
            contador++;
        }
        pthread_mutex_unlock(&mural->lock);

        for (int i = 0; i < MAX_COZINHEIROS; i++) {
            mvprintw(3 + contador + i, 0, "Cozinheiro %d: %s", i + 1, cozinheiros[i].ocupado ? "Ocupado" : "Disponível");
        }

        refresh();
        if (tempo_jogo >= max_tempo_jogo) { // Verificar se o tempo acabou
            break;
        }
        sleep(1); // Esperar 1 segundo antes de atualizar a tela novamente
    }
    
    endwin();
    return NULL;
}

int obter_bancada_disponivel() {   
    for (int i = 0; i < NUM_BANCADAS; i++) {
        if (!bancadas[i].ocupado) {
            return i;
        }
    }
    return -1;
}

void* processar_pedidos(void* arg) {
    Cozinheiro* cozinheiro = (Cozinheiro*)arg;
    while (1) {
        pthread_mutex_lock(&cozinheiro->lock);
        while (cozinheiro->pedido_atual == NULL) {
            pthread_cond_wait(&cozinheiro->cond, &cozinheiro->lock);
        }
        Pedido* pedido = cozinheiro->pedido_atual;
        pthread_mutex_unlock(&cozinheiro->lock);

        // Esperar por uma bancada de preparo disponível
        pthread_mutex_lock(&lock_bancada);
        int bancada_id;
        while ((bancada_id = obter_bancada_disponivel()) == -1) { //Item 5.3 Assimetria de Cozinheiros e Bancadas
            pthread_cond_wait(&cond_bancada, &lock_bancada);
        }
        bancadas[bancada_id].ocupado = 1;
        pthread_mutex_unlock(&lock_bancada);

        // Preparar ingredientes na bancada
        printf("Cozinheiro %d preparando ingredientes do pedido: %s\n", cozinheiro->id, pedido->nome);
        sleep(pedido->tempo_preparo);

        // Liberar a bancada de preparo
        pthread_mutex_lock(&lock_bancada);
        bancadas[bancada_id].ocupado = 0;
        pthread_cond_signal(&cond_bancada);
        pthread_mutex_unlock(&lock_bancada);

        // Cozinhar o pedido
        pthread_mutex_lock(&cozinheiro->lock);
        cozinheiro->ocupado = 1;
        pthread_mutex_unlock(&cozinheiro->lock);

        printf("Cozinheiro %d cozinhando o pedido: %s\n", cozinheiro->id, pedido->nome);
        sleep(pedido->tempo_cozinha);

        printf("Cozinheiro %d concluiu o pedido: %s\n", cozinheiro->id, pedido->nome);
        
        pthread_mutex_lock(&mural.lock);
        free(pedido); 
        pthread_mutex_unlock(&mural.lock);

        pthread_mutex_lock(&cozinheiro->lock);
        cozinheiro->pedido_atual = NULL;
        cozinheiro->ocupado = 0;
        pthread_mutex_unlock(&cozinheiro->lock);
    }
    return NULL;
}

void* gerenciar_pedidos(void* arg) {
    while (1) {
        int input = getch();
        if (input != ERR) {
            if (input >= '1' && input <= '3') { // Verifica se a tecla pressionada está entre '1' e '3'
                int cozinheiro_id = input - '1'; // Converte o caractere para o índice do cozinheiro
                Pedido* pedido = obter_proximo_pedido(&mural);
                if (pedido != NULL) {
                    pthread_mutex_lock(&cozinheiros[cozinheiro_id].lock);
                    cozinheiros[cozinheiro_id].pedido_atual = pedido;
                    pthread_cond_signal(&cozinheiros[cozinheiro_id].cond);
                    pthread_mutex_unlock(&cozinheiros[cozinheiro_id].lock);
                    printf("Pedido %s assinalado para Cozinheiro %d\n", pedido->nome, cozinheiro_id + 1);
                } else {
                    printf("Nenhum pedido disponível para assinalar.\n");
                }
            } else if (input == 'q' || input == 'Q') { // Verifica se a tecla pressionada é 'q' ou 'Q' para encerrar o jogo
                printf("Encerrando o jogo.\n");
                pthread_cancel(thread_pedidos);
                pthread_cancel(thread_informacoes);
                for (int i = 0; i < MAX_COZINHEIROS; i++) {
                    pthread_cancel(thread_cozinheiros[i]);
                }
                pthread_cancel(thread_gerente);
                endwin();
                exit(0);
            }
        }
        usleep(100000); // Esperar 100ms antes de verificar novamente
    }
    return NULL;
}

int main() {
    inicializar_mural(&mural);

    // Inicializar mutexes e condições
    pthread_mutex_init(&lock_bancada, NULL);
    pthread_mutex_init(&lock_cozinha, NULL);
    pthread_cond_init(&cond_bancada, NULL);
    pthread_cond_init(&cond_cozinha, NULL);

    pthread_create(&thread_pedidos, NULL, mural_pedidos, &mural);
    pthread_create(&thread_informacoes, NULL, exibir_informacoes, &mural);
    pthread_create(&thread_gerente, NULL, gerenciar_pedidos, NULL);

    for (int i = 0; i < MAX_COZINHEIROS; i++) {
        cozinheiros[i].id = i + 1;
        cozinheiros[i].ocupado = 0;
        cozinheiros[i].pedido_atual = NULL;
        pthread_mutex_init(&cozinheiros[i].lock, NULL);
        pthread_cond_init(&cozinheiros[i].cond, NULL);
        pthread_create(&thread_cozinheiros[i], NULL, processar_pedidos, &cozinheiros[i]);
    }

    for (int i = 0; i < NUM_BANCADAS; i++) {
        bancadas[i].id = i + 1;
        bancadas[i].ocupado = 0;
        pthread_mutex_init(&bancadas[i].lock, NULL);
        pthread_cond_init(&bancadas[i].cond, NULL);
    }

    while (1) {
        sleep(1); // Esperar 1 segundo antes de verificar novamente
        if (tempo_jogo >= max_tempo_jogo) { // Verificar se o tempo de jogo acabou
            break;
        }
        tempo_jogo++;
    }

    // Cancelar as threads
    pthread_cancel(thread_pedidos);
    pthread_cancel(thread_informacoes);
    for (int i = 0; i < MAX_COZINHEIROS; i++) {
        pthread_cancel(thread_cozinheiros[i]);
    }
    pthread_cancel(thread_gerente);

    // Destruir mutexes e condições
    pthread_mutex_destroy(&lock_bancada);
    pthread_mutex_destroy(&lock_cozinha);
    pthread_cond_destroy(&cond_bancada);
    pthread_cond_destroy(&cond_cozinha);

    for (int i = 0; i < MAX_COZINHEIROS; i++) {
        pthread_mutex_destroy(&cozinheiros[i].lock);
        pthread_cond_destroy(&cozinheiros[i].cond);
    }

    for (int i = 0; i < NUM_BANCADAS; i++) {
        pthread_mutex_destroy(&bancadas[i].lock);
        pthread_cond_destroy(&bancadas[i].cond);
    }

    pthread_mutex_destroy(&mural.lock);

    return 0;
}
