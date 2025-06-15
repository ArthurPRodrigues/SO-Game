# 🚁 Jogo de Sincronização de Threads - Sistemas Operacionais

Este projeto é um jogo interativo em terminal desenvolvido em C++ com `pthreads` e `ncurses`, como parte da disciplina de Sistemas Operacionais.

O objetivo é transportar soldados com um helicóptero entre plataformas, desviando de foguetes lançados por baterias inimigas.

---

## Requisitos

- Sistema Linux (Ubuntu/Mint/Debian)
- Terminal
- `g++` com suporte a pthreads
- Biblioteca `ncurses`

### Instalar dependências no Linux:

```bash
sudo apt update
sudo apt install build-essential libncurses5-dev libncursesw5-dev
```

---

### Threads e Concorrência

-> rocketThread
Thread responsável por mover os foguetes horizontalmente na tela. Utiliza mutex para proteger o acesso à lista de foguetes em uso. Se um disparo do foguete colide com o helicóptero, o jogo é imediatamente encerrado com a mensagem "Game Over".

-> batteryThread
Gerencia o disparo e recarregamento das baterias. Enquanto o jogo estiver ativo e houver foguetes disponíveis, a bateria dispara em posições verticais aleatórias usando rand(). Após esgotar os disparos, a thread simula o processo de recarga e sincroniza o acesso com mutexes.

-> helicopterThread
Controla a lógica do helicóptero. Valida se ainda há soldados a serem transportados e, caso contrário, encerra o jogo com sucesso. A thread gerencia a coleta na base de origem e a entrega na base de destino, além de verificar colisões com baterias e foguetes. Em caso de impacto, a aplicação exibe "Game Over" e finaliza.

---

### Inicialização do projeto

O método main() é responsável por iniciar a aplicação e apresentar o menu de escolha da dificuldade. Após a seleção, o jogo inicia e as threads principais (helicopter, battery0, battery1) são criadas.

Durante a execução, o jogador pode mover o helicóptero pelas teclas direcionais, enquanto as baterias disparam foguetes de forma assíncrona.

Ao final do jogo (seja por sucesso ou colisão), pthread_join é utilizado para aguardar a finalização de todas as threads, garantindo a liberação segura dos recursos de memória e encerrando o jogo corretamente.

### Como jogar

Setas direcionais (↑ ↓ ← →): movimentam o helicóptero

Enter: seleciona a dificuldade no menu

q: encerra o jogo manualmente

Objetivo: transportar 10 soldados da base de origem até a base de destino, sem ser atingido


    
