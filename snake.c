#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <termio.h>
#include <time.h>
#include <stdbool.h>

#define BLANK 0
#define HEAD 1
#define TAIL 2
#define FOOD 9
#define WIDTH 60
#define HEIGHT 20

typedef struct {
    int x;
    int y;
} position;

typedef struct {
    int field[WIDTH][HEIGHT];
    position currentPos;
    int inputDirection;
    char direction;
    int tailLen;
} SharedData;

typedef struct Node {
    position data;
    struct Node* next;
} Node;

typedef struct Queue {
    Node* front;
    Node* rear;
    int length;
} Queue;

static struct termio savemodes;
static int havemodes = 0;

int tty_break();
int tty_getchar();
void resetField();
void printField(int field[WIDTH][HEIGHT]);
void setItem(int field[WIDTH][HEIGHT], int x, int y, int item);
void move(SharedData *data, int key);
void update(SharedData *data, Queue* queue);

Queue* createQueue();
void enqueue(Queue* queue, position data);
position* dequeue(Queue* queue);
int isEmpty(Queue* queue);
int getLength(Queue* queue);
void freeQueue(Queue* queue);

int main() {
    srand(time(NULL));
    int key = 0;
    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget fail");
        exit(1);
    }

    SharedData *data = shmat(shm_id, NULL, 0);
    if (data == (void *) -1) {
        perror("shmat fail");
        exit(1);
    }

    Queue* queue = (Queue*)malloc(sizeof(Queue));
    if (!queue) {
        perror("Failed to allocate memory for queue");
        exit(1);
    }

    resetField();

    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            data->field[i][j] = BLANK;
        }
    }

    data->currentPos.x = WIDTH / 2;
    data->currentPos.y = HEIGHT / 2;
    data->direction='d';
    data->tailLen=3;
    setItem(data->field, 50, 50, FOOD);


    pid_t p = fork();
    if (p < 0) {
        perror("fork fail");
        exit(1);
    }

    // Kindprozess
    else if (p == 0) {
        while (1) {
            resetField();
            printField(data->field);
            usleep(100000);
            update(data, queue);
        }
    }

    // Elternprozess
    else {
        tty_break();
        while (key != 'x') {
            key = tty_getchar();
            move(data, key);
        }
        kill(p, SIGTERM);
        wait(NULL);
    }

    // Shared Memory freigeben
    shmdt(data);
    shmctl(shm_id, IPC_RMID, NULL);

    printf("\n\n");
    return 0;
}
void update(SharedData *data, Queue* queue){
    if(data->inputDirection=='w' && data->direction!='s') data->direction = 'w';
    if(data->inputDirection=='d' && data->direction!='a') data->direction = 'd';
    if(data->inputDirection=='s' && data->direction!='w') data->direction = 's';
    if(data->inputDirection=='a' && data->direction!='d') data->direction = 'a';
    if(getLength(queue) == data->tailLen){
        position* removedPos = dequeue(queue);
        if (removedPos) {
            setItem(data->field, removedPos->x, removedPos->y, BLANK);
            free(removedPos);
        }
    }
    enqueue(queue, data->currentPos);
    setItem(data->field, data->currentPos.x, data->currentPos.y, TAIL);
    switch (data->direction) {
        case 'w':
            if(data->currentPos.y==0) data->currentPos.y = HEIGHT-1;
            else data->currentPos.y--;
            break;
        case 's':
            if(data->currentPos.y==HEIGHT-1) data->currentPos.y = 0;
            else data->currentPos.y++;
            break;
        case 'd':
            if(data->currentPos.x==WIDTH-1) data->currentPos.x = 0;
            else data->currentPos.x++;
            break;
        case 'a':
            if(data->currentPos.x==0) data->currentPos.x = WIDTH-1;
            else data->currentPos.x--;
            break;
    }
    if(data->field[data->currentPos.x][data->currentPos.y] == 9){
        data->tailLen++;
        bool foundPos = false;
        int randX = 0;
        int randY = 0;
        while (foundPos == false){
            randX = (rand() % WIDTH);
            randY = (rand() % HEIGHT);
            if(data->field[randX][randY] == 0)  foundPos = true;
        }
        setItem(data->field, randX, randY, FOOD);
    }else if(data->field[data->currentPos.x][data->currentPos.y] == 2){
        exit(0);
    }
    setItem(data->field, data->currentPos.x, data->currentPos.y, HEAD);
}

void move(SharedData *data, int key) {
    data->inputDirection = key;
}

void setItem(int field[WIDTH][HEIGHT], int x, int y, int item) {
    field[x][y] = item;
}

void resetField() {
    system("clear");
}

void printField(int field[WIDTH][HEIGHT]) {
    char print_char;
    printf("|");
    for (int i = 0; i < WIDTH*2; i++) {
        printf("-");
    }
    printf("|\n");

    for (int i = 0; i < HEIGHT; i++) {
        printf("|");
        for (int j = 0; j < WIDTH; j++) {
            switch (field[j][i]) {
                case BLANK:
                    print_char = ' ';
                    break;
                case HEAD:
                    print_char = 'o';
                    break;
                case TAIL:
                    print_char = '*';
                    break;
                case FOOD:
                    print_char = 'A';
                    break;
            }
            printf("%c ", print_char);
        }
        printf("|\n");
    }

    printf("|");
    for (int i = 0; i < WIDTH*2; i++) {
        printf("-");
    }
    printf("|\n");
}

int tty_break() {
    struct termio modmodes;
    if (ioctl(fileno(stdin), TCGETA, &savemodes) < 0)
        return -1;
    havemodes = 1;
    modmodes = savemodes;
    modmodes.c_lflag &= ~ICANON;
    modmodes.c_cc[VMIN] = 1;
    modmodes.c_cc[VTIME] = 0;
    return ioctl(fileno(stdin), TCSETAW, &modmodes);
}

int tty_getchar() {
    return getchar();
}


Queue* createQueue() {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->front = queue->rear = NULL;
    queue->length = 0;
    return queue;
}

void enqueue(Queue* queue, position data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    newNode->data = data;
    newNode->next = NULL;

    if (queue->rear == NULL) {
        queue->front = queue->rear = newNode;
    } else {
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
    queue->length++;
}

position* dequeue(Queue* queue) {
    if (queue->front == NULL) {
        printf("Queue is empty!\n");
        return NULL;
    }

    Node* temp = queue->front;
    position* data = malloc(sizeof(position)); // Allocate new position
    if (data) {
        *data = temp->data; // Copy data
    }

    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }

    free(temp);
    queue->length--;
    return data; // Return the pointer to the copied position
}

int isEmpty(Queue* queue) {
    return queue->length == 0;
}

int getLength(Queue* queue) {
    return queue->length;
}

void freeQueue(Queue* queue) {
    while (!isEmpty(queue)) {
        dequeue(queue);
    }
    free(queue);
}