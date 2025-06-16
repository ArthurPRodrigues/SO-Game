#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <algorithm>

// Definições de tamanho da tela e configurações do modo fácil
#define SCREEN_HEIGHT 20
#define SCREEN_WIDTH 60
#define MAX_SOLDIERS 10
#define RECHARGE_BASE_X 2  // Posição X da base de recarga

// Estrutura para representar posições na tela
struct Position {
    int x, y;
};

// Estrutura do foguete (posição e ID da bateria que o lançou)
struct Rocket {
    int x, y, batteryId;
};

// Estrutura para armazenar o estado de cada bateria
struct Battery {
    int x;
    int rocketsLeft;
    bool isRecharging;
};

// Enumeração e estrutura para configurações de dificuldade
enum Difficulty { EASY, MEDIUM, HARD };
Difficulty selectedDifficulty;

struct DifficultySettings {
    int maxRockets;
    int PeriodBetweenRocketsS;
    int ReloadTimeMs;
};

// Tabela de configurações por dificuldade
DifficultySettings possibleSettings[] = {
    {2, 5, 500},   // Fácil
    {4, 3, 250},   // Médio
    {8, 1, 100}    // Difícil
};
DifficultySettings currentSettings;

// Variáveis globais de estado
Position platformOrigin, platformDest, helicopter;
bool carryingSoldier = false;
std::vector<pthread_t> activeRockets;
std::vector<Rocket> activeRocketPositions;
const int batteryY = SCREEN_HEIGHT - 2;  // Posição Y fixa na parte inferior
Battery battery0 = {15, 0, false};
Battery battery1 = {45, 0, false};
int soldiersTransported = 0;
bool gameRunning = true;

// Mutexes para sincronização
pthread_mutex_t screenMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rocketListMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rechargeMutex = PTHREAD_MUTEX_INITIALIZER;  // Controla acesso à recarga

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

void messageOnScreen(const std::string& message) {
    pthread_mutex_lock(&screenMutex);
    werase(gamewin);
    mvwprintw(gamewin, SCREEN_HEIGHT/2, SCREEN_WIDTH/2 - (int)message.length()/2, "%s", message.c_str());
    box(gamewin, 0, 0);
    wrefresh(gamewin);
    pthread_mutex_unlock(&screenMutex);
}

void drawScreen() {
    pthread_mutex_lock(&screenMutex);
    werase(gamewin);

    // Cabeçalho e contadores
    mvwprintw(gamewin, 1, 0, "== Pressione 'q' para sair === Soldados transportados: %d/%d=", soldiersTransported, MAX_SOLDIERS);
    mvwprintw(gamewin, 2, 0, "===========================================================");

    // Plataformas e baterias
    mvwprintw(gamewin, platformOrigin.y, platformOrigin.x, "_");
    mvwprintw(gamewin, platformDest.y, platformDest.x, "_");
    
    // Base de recarga
    mvwprintw(gamewin, batteryY, RECHARGE_BASE_X, "[R]");
    
    // Baterias com contagem de foguetes
    mvwprintw(gamewin, batteryY, battery0.x, "B0(%d)%s", battery0.rocketsLeft, battery0.isRecharging ? "R" : "");
    mvwprintw(gamewin, batteryY, battery1.x, "B1(%d)%s", battery1.rocketsLeft, battery1.isRecharging ? "R" : "");

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
    mvwprintw(gamewin, SCREEN_HEIGHT - 1, 0, "===========================================================");
    box(gamewin, 0, 0);
    wrefresh(gamewin);
    pthread_mutex_unlock(&screenMutex);
}

void* rocketThread(void* arg) {
    Rocket rocket = *(Rocket*)arg;
    delete (Rocket*)arg;

    while (gameRunning && rocket.y >= 0) {
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

        // Move o foguete para cima
        pthread_mutex_lock(&rocketListMutex);
        for (auto& r : activeRocketPositions) {
            if (r.batteryId == rocket.batteryId && r.x == rocket.x && r.y == rocket.y) {
                r.y--;
            }
        }
        pthread_mutex_unlock(&rocketListMutex);

        rocket.y--;
        usleep(80000); // Delay entre movimentos
    }

    // Remove o foguete da lista após terminar
    pthread_mutex_lock(&rocketListMutex);
    activeRocketPositions.erase(std::remove_if(activeRocketPositions.begin(), activeRocketPositions.end(),
        [&](Rocket& r){ return r.x == rocket.x && r.y == rocket.y && r.batteryId == rocket.batteryId; }), activeRocketPositions.end());
    pthread_mutex_unlock(&rocketListMutex);

    return nullptr;
}

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

void* batteryThread(void* arg) {
    int batteryId = *(int*)arg;
    delete (int*)arg;

    Battery* battery = (batteryId == 0) ? &battery0 : &battery1;
    battery->rocketsLeft = currentSettings.maxRockets;
    int minX = 5, maxX = SCREEN_WIDTH - 5;

    while (gameRunning) {
        if (battery->rocketsLeft > 0) {
            battery->isRecharging = false;
            int batteryX = minX + rand() % (maxX - minX + 1);
            battery->x = batteryX;

            fireRocketFromBattery(batteryX, batteryY - 1, batteryId);
            battery->rocketsLeft--;
        } else {
            // Verifica se outra bateria já está recarregando
            bool otherBatteryRecharging = (batteryId == 0) ? battery1.isRecharging : battery0.isRecharging;
            
            if (!otherBatteryRecharging) {
                // Bloqueia o mutex antes de começar a recarga
                pthread_mutex_lock(&rechargeMutex);
                battery->isRecharging = true;
                
                // Move para a base de recarga
                while (battery->x > RECHARGE_BASE_X + 4 && gameRunning) {
                    battery->x--;
                    drawScreen();
                    usleep(100000);
                }
                
                // Processo de recarga
                while (battery->rocketsLeft < currentSettings.maxRockets && gameRunning) {
                    usleep(currentSettings.ReloadTimeMs * 1000);
                    battery->rocketsLeft++;
                    drawScreen();
                }
                
                // Move de volta para posição de combate
                while (battery->x < minX && gameRunning) {
                    battery->x++;
                    drawScreen();
                    usleep(100000);
                }
                
                battery->isRecharging = false;
                pthread_mutex_unlock(&rechargeMutex);
            } else {
                // Outra bateria está recarregando, espera um pouco
                usleep(500000);
            }
        }

        drawScreen();
        sleep(currentSettings.PeriodBetweenRocketsS);
    }
    return nullptr;
}

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
            platformDest.x = 10 + rand() % (SCREEN_WIDTH - 20); // Muda destino horizontalmente
        }

        // Verifica colisão com baterias ou parede
        if ((currentPos.y == batteryY && (currentPos.x == battery0.x || currentPos.x == battery1.x)) || currentPos.x <= 0 || currentPos.x >= SCREEN_WIDTH - 1 || currentPos.y <= 0 || currentPos.y >= SCREEN_HEIGHT - 1) {
            messageOnScreen("Game Over. Helicopter was destroyed.");
            gameRunning = false;
            break;
        }

        drawScreen();
        usleep(100000);
    }
    return nullptr;
}

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

    // Inicializa baterias
    battery0.rocketsLeft = currentSettings.maxRockets;
    battery1.rocketsLeft = currentSettings.maxRockets;

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