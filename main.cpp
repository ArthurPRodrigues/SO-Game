#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>
#include <vector> //array, mas dinâmico
#include <cstdlib> // rand() e srand()
#include <ctime> // time(NULL)
#include <algorithm> //std::remove_if

// Definições de tamanho da tela e configurações do modo fácil
#define SCREEN_HEIGHT 20
#define SCREEN_WIDTH 60
#define MAX_SOLDIERS 10

// Estrutura para representar posições na tela
struct Position {
    int x, y;
};

// Estrutura do foguete (posição e ID da bateria que o lançou)
struct Rocket {
    int x, y, batteryId;
};

// Enumeração e estrutura para configurações de dificuldade
enum Difficulty { EASY, MEDIUM, HARD };
Difficulty selectedDifficulty;

// 
struct DifficultySettings {
    int maxRockets;
    int PeriodBetweenRocketsS;
    int ReloadTimeMs;
};

// Tabela de configurações por dificuldade
DifficultySettings possibleSettings[] = {
    {1, 5, 500},   // Fácil
    {4, 3, 250},   // Médio
    {8, 1, 100}   // Difícil
};
DifficultySettings currentSettings;

// Variáveis globais de estado
Position platformOrigin, platformDest, helicopter;
bool carryingSoldier = false;
std::vector<pthread_t> activeRockets;
std::vector<Rocket> activeRocketPositions;
const int batteryX = SCREEN_WIDTH - 5;
int battery0Y = 3;
int battery1Y = 7;
int soldiersTransported = 0;
bool gameRunning = true;

// Mutexes para sincronização
pthread_mutex_t screenMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rocketListMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t depositMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bridgeMutex = PTHREAD_MUTEX_INITIALIZER;

// Janela principal do jogo (ncurses)
WINDOW* gamewin;

Difficulty chooseDifficulty() {
    const char* options[] = {"Easy", "Normal", "Hard"};
    int choice = 1;
    int input;

    while(true) {
        clear();
        mvprintw(5, 10, "Difficulty:");
        for (int i = 0; i < 3; ++i) {
            if (i == choice)
                mvprintw (7 + i, 12, "-> %s", options[i]);
            else
                mvprintw (7 + i, 12, "   %s", options[i]);
        }
        refresh();

        input = getch();
        switch(input) {
            case KEY_UP:
                if (choice > 0) choice--;
                break;
            case KEY_DOWN:
                if (choice < 2) choice++;
                break;
            case 10: // tecla enter
                return static_cast<Difficulty>(choice);
        }
    }
}

// Função para mostrar mensagens centrais na tela (ex: game over)
void messageOnScreen(const std::string& message) {
    pthread_mutex_lock(&screenMutex);
    werase(gamewin);
    mvwprintw(gamewin, SCREEN_HEIGHT/2, SCREEN_WIDTH/2 - (int)message.length()/2, "%s", message.c_str());
    box(gamewin, 0, 0);
    wrefresh(gamewin);
    pthread_mutex_unlock(&screenMutex);
}

// Função para desenhar todos os elementos na tela
void drawScreen() {
    pthread_mutex_lock(&screenMutex);
    werase(gamewin);

    // Cabeçalho e contadores
    mvwprintw(gamewin, 1, 0, "== Pressione 'q' para sair === Soldados transportados: %d/%d=", soldiersTransported, MAX_SOLDIERS);
    mvwprintw(gamewin, 2, 0, "===========================================================");

    // Plataformas e baterias
    mvwprintw(gamewin, platformOrigin.y, platformOrigin.x, "_");
    mvwprintw(gamewin, platformDest.y, platformDest.x, "_");
    mvwprintw(gamewin, battery0Y, batteryX, "B0");
    mvwprintw(gamewin, battery1Y, batteryX, "B1");

    // Helicóptero
    mvwprintw(gamewin, helicopter.y, helicopter.x, carryingSoldier ? "H[S]" : "H[-]");

    // Foguetes
    pthread_mutex_lock(&rocketListMutex);
    for (const auto& rocket : activeRocketPositions) {
        if (rocket.x >= 0 && rocket.x < SCREEN_WIDTH && rocket.y >= 0 && rocket.y < SCREEN_HEIGHT) {
            mvwprintw(gamewin, rocket.y, rocket.x, "*");
        }
    }
    pthread_mutex_unlock(&rocketListMutex);

    // Rodapé e borda
    mvwprintw(gamewin, SCREEN_HEIGHT - 2, 0, "===========================================================");
    box(gamewin, 0, 0);
    wrefresh(gamewin);
    pthread_mutex_unlock(&screenMutex);
}

// Thread que move o foguete horizontalmente até sair da tela ou atingir o helicóptero
void* rocketThread(void* arg) {
    Rocket rocket = *(Rocket*)arg;
    delete (Rocket*)arg;

    while (gameRunning && rocket.x >= 0) {
        pthread_mutex_lock(&rocketListMutex);
        for (auto& r : activeRocketPositions) {
            if (r.batteryId == rocket.batteryId && r.x == rocket.x + 1) {
                r.x = rocket.x;
            }
        }
        pthread_mutex_unlock(&rocketListMutex);

        // Verifica colisão com o helicóptero
        bool hit = false;
        pthread_mutex_lock(&screenMutex);
        hit = (rocket.y == helicopter.y && rocket.x == helicopter.x);
        pthread_mutex_unlock(&screenMutex);

        if (hit) {
            messageOnScreen("Game over. Helicopter hit by rocket.");
            gameRunning = false;
            break;
        }

        // Move o foguete
        pthread_mutex_lock(&rocketListMutex);
        for (auto& r : activeRocketPositions) {
            if (r.batteryId == rocket.batteryId && r.x == rocket.x + 1) {
                r.x--;
            }
        }
        pthread_mutex_unlock(&rocketListMutex);

        rocket.x--;
        usleep(80000); // Delay entre movimentos
    }

    // Remove o foguete da lista após terminar
    pthread_mutex_lock(&rocketListMutex);
    activeRocketPositions.erase(std::remove_if(activeRocketPositions.begin(), activeRocketPositions.end(),
        [&](Rocket& r){ return r.x == rocket.x && r.y == rocket.y && r.batteryId == rocket.batteryId; }), activeRocketPositions.end());
    pthread_mutex_unlock(&rocketListMutex);

    return nullptr;
}

// Função que dispara um novo foguete a partir da bateria
void fireRocketFromBattery(int x, int y, int batteryId) {
    Rocket* rocket = new Rocket{.x = x, .y = y, .batteryId = batteryId};
    pthread_mutex_lock(&rocketListMutex);
    activeRocketPositions.push_back(*rocket);
    pthread_mutex_unlock(&rocketListMutex);

    pthread_t t;
    pthread_create(&t, nullptr, rocketThread, rocket);
    pthread_mutex_lock(&rocketListMutex);
    activeRockets.push_back(t);
    pthread_mutex_unlock(&rocketListMutex);
}

// Thread de uma bateria: dispara foguetes até acabar, depois vai recarregar
void* batteryThread(void* arg) {
    int batteryId = *(int*)arg;
    delete (int*)arg;

    int rocketsLeft = currentSettings.maxRockets;
    int minY = 3, maxY = SCREEN_HEIGHT - 3;

    while (gameRunning) {
        if (rocketsLeft > 0) {
            int batteryY = minY + rand() % (maxY - minY + 1);
            if (batteryId == 0) battery0Y = batteryY;
            else battery1Y = batteryY;

            fireRocketFromBattery(batteryX, batteryY, batteryId);
            rocketsLeft--;
        } else {
            // Sincronização para atravessar a ponte e recarregar
            pthread_mutex_lock(&bridgeMutex);
            pthread_mutex_lock(&depositMutex);

            usleep(currentSettings.ReloadTimeMs*1000);  // Simula o tempo de recarga
            rocketsLeft = currentSettings.maxRockets;

            pthread_mutex_unlock(&depositMutex);
            pthread_mutex_unlock(&bridgeMutex);
        }

        drawScreen();
        sleep(currentSettings.PeriodBetweenRocketsS);
    }
    return nullptr;
}

// Thread que controla a lógica do helicóptero (movimento, entrega de soldados, colisões)
void* helicopterThread(void* arg) {
    while (gameRunning) {
        if (soldiersTransported >= MAX_SOLDIERS) {
            messageOnScreen("Mission accomplished!");
            gameRunning = false;
            break;
        }

        pthread_mutex_lock(&screenMutex);
        Position currentPos = helicopter;
        pthread_mutex_unlock(&screenMutex);

        // Pega soldado
        if (currentPos.x == platformOrigin.x && currentPos.y == platformOrigin.y && !carryingSoldier) {
            carryingSoldier = true;
        }
        // Entrega soldado
        else if (currentPos.x == platformDest.x && currentPos.y == platformDest.y && carryingSoldier) {
            carryingSoldier = false;
            soldiersTransported++;
            platformDest.y = 3 + rand() % (SCREEN_HEIGHT - 6); // Muda destino verticalmente
        }

        // Verifica colisão com baterias ou parede
        if ((currentPos.x == batteryX && (currentPos.y == battery0Y || currentPos.y == battery1Y)) ||
            currentPos.y <= 2 || currentPos.y >= SCREEN_HEIGHT - 2) {
            messageOnScreen("Game Over. Helicopter was destroyed.");
            gameRunning = false;
            break;
        }

        drawScreen();
        usleep(100000);
    }
    return nullptr;
}

// Função principal: inicialização da tela, criação das threads e controle de entrada do jogador
int main() {

    // Window inicial para escolha da dificuldade
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    selectedDifficulty = chooseDifficulty();
    currentSettings = possibleSettings[selectedDifficulty];
    sleep(1);
    endwin();

    // Window do jogo
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    gamewin = newwin(SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0);
    keypad(gamewin, TRUE);
    nodelay(gamewin, TRUE);

    // Inicializa posições
    platformOrigin = {1, SCREEN_HEIGHT / 2};
    platformDest = {SCREEN_WIDTH-20, SCREEN_HEIGHT / 2};
    helicopter = platformOrigin;

    // Cria as threads do jogo
    pthread_t heliThread, bat0Thread, bat1Thread;
    pthread_create(&heliThread, nullptr, helicopterThread, nullptr);
    pthread_create(&bat0Thread, nullptr, batteryThread, new int(0));
    pthread_create(&bat1Thread, nullptr, batteryThread, new int(1));

    // Loop principal: lê entrada do jogador
    while (gameRunning) {
        int ch = wgetch(gamewin);
        pthread_mutex_lock(&screenMutex);
        switch (ch) {
            case KEY_UP: if (helicopter.y > 0) helicopter.y--; break;
            case KEY_DOWN: if (helicopter.y < SCREEN_HEIGHT - 1) helicopter.y++; break;
            case KEY_LEFT: if (helicopter.x > 0) helicopter.x--; break;
            case KEY_RIGHT: if (helicopter.x < SCREEN_WIDTH - 1) helicopter.x++; break;
            case 'q': gameRunning = false; break;
        }
        pthread_mutex_unlock(&screenMutex);
        usleep(50000);
    }

    // Espera todas as threads terminarem
    pthread_join(heliThread, nullptr);
    pthread_join(bat0Thread, nullptr);
    pthread_join(bat1Thread, nullptr);
    for (auto& t : activeRockets) pthread_join(t, nullptr);

    endwin();
    return 0;
}
