/*
How the game works. things to keep in mind:

Client connects. Asked for name, added to list of clients

Client added. Waits for another client to be added.

Once two clients are entered, they engage in combat.

Each client has a set number of power moves that are stronger
but not guaranteed to hit. Regular attacks are also available, 
they are guaranteed to land, and relatively weaker.

The probability of hitting the opponent with a powermove is up to you.

Attacking client should also have the option to say something.

The opponent client does not know how many powermoves the client has.

Each client has HP, game ends when HP reaches 0 (or lower).

Once the battle ends, do not immediately start another battle, unless with another client 
(randomly chosen? Choose the first available client that is not the client you just fought against.)

One server should be able to handle multiple clients' battles.

If one client leaves the server for whatever reason, the opponent is declared the winner.

Syntax:
------------------------------------------------------------------------------------------
CLIENT ENTRANCE:
What is your name?Danny
Welcome, Danny! Awaiting opponent...
You engage kl!
Your hitpoints: 30
Your powermoves: 3

kl's hitpoints: 24

(a)ttack
(p)owermove
(s)peak something
a
You hit kl for 4 damage!
Your hitpoints: 30
Your powermoves: 3

kl's hitpoints: 20
Waiting for kl to strike...
**mm enters the arena**
--------------------------------------------------------------------------
RECEIVING DAMAGE:

You engage aa!
Your hitpoints: 25
Your powermoves: 1

aa's hitpoints: 28
Waiting for aa to strike...

aa hits you for 4 damage!
Your hitpoints: 21
Your powermoves: 1

aa's hitpoints: 28

(a)ttack
(p)owermove
(s)peak something

------------------------------------------------------------

ATTACKER SPEAKS:

Hamburger's hitpoints: 27

(a)ttack
(p)owermove
(s)peak something
s
Speak: 
You're trash.
You speak: You're trash.

Your hitpoints: 21
Your powermoves: 1

Hamburger's hitpoints: 27

(a)ttack
(p)owermove
(s)peak something
---------------------------------------------------------------------------
DEFENDER RECEIVES MESSAGE:

Waiting for Bob to strike...
Bob takes a break to tell you:
Hello

Your hitpoints: 21
Your powermoves: 2


--------------------------------------------------------------------------
OPPONENT DROPS OUT:

Bob's hitpoints: 21

(a)ttack
(p)owermove
(s)peak something

--Bob dropped. You win!

Awaiting next opponent...
**Bob leaves**

-------------------------------------------------------------------------
MISSED ATTACK + VICTORY:
Ramy missed you!
Your hitpoints: 16
Your powermoves: 0

Ramy's hitpoints: 5

(a)ttack
(s)peak something
a
You hit Ramy for 6 damage!
Ramy gives up. You win!

Awaiting next opponent...
--------------------------------------------------------------------------
*/ 

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */
#include <arpa/inet.h>     /* only needed on mac */

#define WAITING 1
#define BATTLING 0

#define MAX_BUF 100
#define MAX_CLIENTS 100

typedef struct client {
        // Clients have names, state (In battle, waiting), in_addr
        char *name;
        int state;
        struct sockaddr_in addr;
        struct client *last_opponent;
        int fd;
    } Client;

    typedef struct clientNode {
        struct client client;
        struct clientNode *next;
    } ClientNode;

ClientNode *front = NULL; //beginning of dynamic array

//FUNCTION PROTOTYPES
int accept_player(int listen_soc);
void engage_battle(Client *p1, Client *p2);
void delete_client(int fd);

int main() {

    int listenfd = socket(AF_INET, SOCK_STREAM, 0); //socket
     if (listenfd == -1) {
        perror("server: socket");
        exit(1);
    }
    
    //Assignment provided code. Lets the client connect to server, the moment it leaves.
    int yes = 1;
    if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }


    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(57230);
    memset(&server.sin_zero, 0, 8);
    server.sin_addr.s_addr = INADDR_ANY;


    if (bind(listenfd, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) {
      perror("server: bind");
      close(listenfd);
      exit(1);
    }

    if (listen(listenfd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(1);
    }

    fd_set master_fds; //arbitrary set of file descriptors
    FD_ZERO(&master_fds);
    FD_SET(listenfd, &master_fds); //setting listenfd in master_fds for future use
    int max_fd = listenfd; // setting the number of file descriptors we will be watching for (volatile)

    while (1) {
        fd_set read_fds = master_fds; // setting read_fds to master_fds (readfds is cleared of all fd except those ready to be read)
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) { 
            //select blocks until activity happens in one of the file descriptors or timeout occurs, in this case, no timeout.
            perror("select");
            exit(1);
        }

        for (int fd = 0; fd <= max_fd; fd++) { 
            //Iterate through each file descriptors one by one to see what file descriptor the change occured.
            // A change has to have occured since select returned.
            if (FD_ISSET(fd, &read_fds)) { //Check if fd is in read_fds, which is set of file descriptors that is to be observed.
                if (fd == listenfd) { // If file descriptor is equal to listenfd, that means a client is trying to connect.
                    // New client connection
                    int client_socket = accept_player(listenfd);
                    
                    FD_SET(client_socket, &master_fds); 
                    //Add client socket fd to master_fds to be observed in the future
                    if (client_socket > max_fd) {
                        max_fd = client_socket; // Making sure that you read up till client socket since client socket needs to be observed too.
                    }
                } else {
                    // fd is not equal to listenfd, fd equals one of the client socket fds, some information needs to be read
                    
                    char buf[MAX_BUF + 1];
                    ssize_t bytes_read = read(fd, buf, MAX_BUF);
                    if (bytes_read <= 0) {
                        // Client disconnected or error occurred
                        if (bytes_read == 0) {
                            // Client disconnected
                            ClientNode *curr = front;
                            char buf[MAX_BUF + strlen("** leaves**\r\n") + 1];
                            
                            Client *disconnecting_client;
                            if (front != NULL) {
                                while (curr->next != NULL) {
                                    if (curr->client.fd != fd) {
                                        write(curr->client.fd, buf, strlen(buf));
                                    }
                                    else {
                                        disconnecting_client = &(curr->client);
                                        }
                                    curr = curr->next; 
                                }
                                if (front->client.fd == fd) {disconnecting_client = &front->client;}
                            }
                            sprintf(buf, "**%s leaves**\r\n", disconnecting_client->name);
                        } else {
                            // Error occurred
                            perror("read");
                        }
                        delete_client(fd);
                        close(fd); // Close the socket

                        FD_CLR(fd, &master_fds); // Remove the socket from the master set
                    }
                    // Process data read from the client
                }

            }
        }
         // Check if two clients can battle.
        Client *pair[2] = {NULL, NULL}; //pair that could potentially battle.
        int first_found = 0;

        ClientNode *curr = front;

        //loop through dynamic array, checking if there is a waiting client.
        while (curr != NULL) {
            
            if (curr->client.state == WAITING) {
                //waiting client found.
                // Must check if they have battled before.
                pair[first_found] = &curr->client;
                if (first_found) {
                    //pair has two clients.
                    if (pair[0]->last_opponent == pair[1] || pair[1]->last_opponent == pair[0]) {
                        pair[1] = NULL;
                    }
                }
                first_found = 1;
            }
            curr = curr->next;
        }
        // curr->next is NULL (No pair found) and/or pair found

        if (pair[1] != NULL) {
            // pair found.
            pair[0]->state = BATTLING;
            pair[0]->last_opponent = pair[1];

            pair[1]->state = BATTLING;
            pair[1]->last_opponent = pair[0];


            engage_battle(pair[0], pair[1]);
        }
    }
    return 0;
}

int accept_player(int listen_soc) {
    struct sockaddr_in client_addr;

    unsigned int client_len = sizeof(struct sockaddr_in);

    int client_socket = accept(listen_soc, (struct sockaddr *)&client_addr, &client_len);
    
    if (client_socket == -1) {
        perror("accept");
        exit(1);
    }

    
    if(write(client_socket, "What is your name?\r\n", strlen("What is your name?\r\n")) < 0) {
        perror("write");
        exit(1);
    }
    
    char character;

    Client client;
    client.name = malloc(MAX_BUF);
    client.name[0] = '\0';
    
    int i = 0;
    while (read(client_socket, &character, 1)) {
        if (character == '\n') {
            client.name[i-1] = '\0'; // Null-terminate the name
            break;
        }
        // printf("client.name: %s, character: %c, Available Space: %ld\n",
            // client.name, character, MAX_BUF - strlen(client.name) - 1);
        client.name[i] = character;
        i++;
    }
    
    client.addr = client_addr;
    client.state = WAITING;
    client.last_opponent = NULL;
    client.fd = client_socket;
    
    ClientNode *curr = front;
    char buf[MAX_BUF + strlen("** enters the arena**\r\n") + 1];
    sprintf(buf, "**%s enters the arena**\r\n", client.name);
    

    if (front != NULL) {
        ssize_t bytes_written;
        while (curr->next != NULL) {
            if ((bytes_written = write(curr->client.fd, buf, strlen(buf)) < 0)) {
                perror("write");
                exit(1);
            } else if (bytes_written == 0) {
                close(curr->client.fd);
                delete_client(curr->client.fd);
            }
            
            curr = curr->next;
        }
        ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
        node->client = client;
        node->next = NULL;
        curr->next = node;
    } else {
        
        ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
        node->client = client;
        node->next = NULL;
        front = node;
    }

    // nc -C localhost 57230
    //Client added to dynamic array.

    char welcome_message[MAX_BUF + strlen("Welcome ! Awaiting opponent...\r\n") + 1];

    sprintf(welcome_message, "Welcome %s! Awaiting opponent...\r\n", client.name);

    if(write(client_socket, welcome_message, strlen(welcome_message)) < 0) {
        perror("write");
        exit(1);
    }
    return client_socket;
}

void engage_battle(Client *p1, Client *p2) {
    
    fd_set read_fds;
    struct timeval timeout;


    // Initialize hitpoints and power moves
    int p1_hp = 30;
    int p2_hp = 30;
    int p1_pm = 3;
    int p2_pm = 3;

    char buf[MAX_BUF + 1]; // Buffer for messages

    // Inform players about the engagement
    sprintf(buf, "You engage %s!\r\n", p2->name);
    write(p1->fd, buf, strlen(buf));
    sprintf(buf, "You engage %s!\r\n", p1->name);
    write(p2->fd, buf, strlen(buf));

    // Loop until one of the players runs out of hitpoints
    int i = 0;
    while (p1_hp > 0 && p2_hp > 0) {
        // Set up read_fds with the two clients' sockets
        FD_ZERO(&read_fds);
        FD_SET(p1->fd, &read_fds);
        FD_SET(p2->fd, &read_fds);

        timeout.tv_sec = 1; // Adjust timeout as needed
        timeout.tv_usec = 0;

        int ready = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);
        if (ready == -1) {
            perror("select");
            // Handle error, if any
        } else {
            // Check if p1 disconnected
            if (FD_ISSET(p1->fd, &read_fds)) {
                char buf[MAX_BUF];
                ssize_t bytes_read = read(p1->fd, buf, MAX_BUF);
                if (bytes_read <= 0) {
                    // Client disconnected
                    sprintf(buf, "--%s dropped. You win!\r\n\nAwaiting next opponent...\r\n", p2->name);
                    write(p2->fd, buf, strlen(buf));
                    // Update p2 as the winner
                    p1_hp = 0; // End the battle
                    break;
                }
            }
            // Check if p2 disconnected
            if (FD_ISSET(p2->fd, &read_fds)) {
                char buf[MAX_BUF];
                ssize_t bytes_read = read(p2->fd, buf, MAX_BUF);
                if (bytes_read <= 0) {
                    // Client disconnected
                    sprintf(buf, "--%s dropped. You win!\r\n\nAwaiting next opponent...\r\n", p1->name);
                    write(p1->fd, buf, strlen(buf));
                    // Update p1 as the winner
                    p2_hp = 0; // End the battle
                    break;
                }
            }
        }

        Client *attacker, *defender;
        if (i % 2 == 0) {
            attacker = p1;
            defender = p2;
        } else {
            attacker = p2;
            defender = p1;
        }

        // Inform the attacker about their status
        sprintf(buf, "Your hitpoints: %d\r\n", i % 2 == 0 ? p1_hp : p2_hp);
        write(attacker->fd, buf, strlen(buf));
        if (!(( i % 2 == 0 && p1_pm == 0) || ( i % 2 == 1 && p2_pm == 0))) { 
            sprintf(buf, "Your powermoves: %d\r\n", i % 2 == 0 ? p1_pm : p2_pm);
            write(attacker->fd, buf, strlen(buf));
        }
        // Inform the attacker about the defender's status
        sprintf(buf, "\n%s's hitpoints: %d\r\n", defender->name, (i + 1) % 2 == 0 ? p1_hp : p2_hp);
        write(attacker->fd, buf, strlen(buf));

        // Inform the defender about their status and the attacker's hp.
        sprintf(buf, "Your hitpoints: %d\r\n", (i + 1) % 2 == 0 ? p1_hp : p2_hp);
        write(defender->fd, buf, strlen(buf));
        sprintf(buf, "Your powermoves: %d\r\n", (i + 1) % 2 == 0 ? p1_pm : p2_pm);
        write(defender->fd, buf, strlen(buf));
        sprintf(buf, "\n%s's hitpoints: %d\r\n", attacker->name, (i) % 2 == 0 ? p1_hp : p2_hp);
        write(defender->fd, buf, strlen(buf));

        // Inform the defender to wait for the attacker to strike
        sprintf(buf, "Waiting for %s to strike...\r\n\r\n", attacker->name);
        write(defender->fd, buf, strlen(buf));

        // Provide options for the attacker
        write(attacker->fd, "\n(a)ttack\r\n", strlen("\n(a)ttack\r\n"));
        if (((i % 2 == 0) && p1_pm > 0) || ((i % 2 == 1) && p2_pm > 0)) {
            write(attacker->fd, "(p)owermove\r\n", strlen("(p)owermove\r\n"));
        }
        
        write(attacker->fd, "(s)peak something\r\n", strlen("(s)peak something\r\n"));
        write(attacker->fd, "(r)andom choice between regular attack or powermove\r\n", 
        strlen("(r)andom choice between regular attack or powermove\r\n"));
        // See block selection below

        fd_set set;
        struct timeval timeout;

        // Initialize the file descriptor set.
        FD_ZERO(&set);
        FD_SET(attacker->fd, &set);

        // Initialize the timeout data structure.
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        // select returns 0 if timeout, 1 if input available, -1 if error.
        int rv = select(FD_SETSIZE, &set, NULL, NULL, &timeout);

        if (rv == -1) {
            perror("select"); // error occurred in select()
        } else if (rv == 0) {
            write(attacker->fd, "\nTimeout occurred! No data after 5 seconds.\r\n\n",
                strlen("\nTimeout occurred! No data after 5 seconds.\r\n\n"));
            buf[0] = 'r';
            buf[1] = '\0';
        } else {
            // User input available, read the data.
            if (read(attacker->fd, buf, 1) < 0) {
                perror("read");
            }
            buf[1] = '\0';
            write(attacker->fd, "\r\n", 2);
        }

        // Handle the attacker's choice
        if (strcmp(buf, "a") == 0) {
            // Handle regular attack
            int dmg = 3; // Example damage
            
            i % 2 == 0 ? (p2_hp -= dmg) : (p1_hp -= dmg);
            sprintf(buf, "You hit %s for %d damage!\r\n", defender->name, dmg);
            write(attacker->fd, buf, strlen(buf));

            sprintf(buf, "%s hits you for %d damage!\r\n", attacker->name, dmg);
            write(defender->fd, buf, strlen(buf));
        } else if (strcmp(buf, "p") == 0) {
            // Handle powermove
            int power_attack = rand() % 19 + 12; // Example power move damage
            // HANDLE IF POWERMOVE LANDS OR NOT

            //chance of landing is 33.33%
            int chance = rand() % 3;
            if (chance != 1) {
                sprintf(buf, "%s missed you!\r\n", attacker->name);
                write(defender->fd, buf, strlen(buf));

                write(attacker->fd, "You missed!\r\n", strlen("You missed!\r\n"));
            }
            else {
                i % 2 == 0 ? (p1_pm--) : (p2_pm--);
                i % 2 == 0 ? (p2_hp -= power_attack) : (p1_hp -= power_attack);
                
                sprintf(buf, "You hit %s for %d damage!\r\n", defender->name, power_attack);
                write(attacker->fd, buf, strlen(buf));

                sprintf(buf, "%s hits you for %d damage!\r\n", attacker->name, power_attack);
                write(defender->fd, buf, strlen(buf));}
        } else if (strcmp(buf, "s") == 0) {
            sprintf(buf, "%s takes a break to tell you:\r\n", attacker->name);
            write(defender->fd, buf, strlen(buf));
            
            memset(buf, 0, sizeof(buf));
            write(attacker->fd, "Speak:\r\n", strlen("Speak:\r\n"));
            
            char *msg = malloc(MAX_BUF + 1);
            char character;

            int j = 0;

            while (read(attacker->fd, &character, 1) && j <= MAX_BUF) {
                if (character != '\n') {
                    msg[j] = character;
                    j++;
                }
                else {
                    msg[j] = '\0';
                    break;
                }
            }

            write(attacker->fd, "You speak: ", strlen("You speak:\r\n")); //Msgs before sending msg

            write(attacker->fd, msg, strlen(msg)); //Sending msg to both players
            write(defender->fd, msg, strlen(msg));

            write(attacker->fd, "\n\n", strlen("\n\n"));
            write(defender->fd, "\n\n", strlen("\n\n"));

            free(msg);
            i++; // It is still the attacker's turn.
        } else if (strcmp(buf, "r") == 0) {
            // Handle random number generator move
            int random_choice = rand() % 2;
            int attacker_pm;
            attacker->state == p1->state ? (attacker_pm = p1_pm) : (attacker_pm = p2_pm);
            if (random_choice == 0 || attacker_pm == 0) {
                 // Handle regular attack
                int dmg = 3; // Example damage
                write(attacker->fd, "Random Choice chose regular attack\r\n",
                strlen("Random Choice chose regular attack\r\n"));
                
                i % 2 == 0 ? (p2_hp -= dmg) : (p1_hp -= dmg);
                sprintf(buf, "You hit %s for %d damage!\r\n", defender->name, dmg);
                write(attacker->fd, buf, strlen(buf));

                sprintf(buf, "%s hits you for %d damage!\r\n", attacker->name, dmg);
                write(defender->fd, buf, strlen(buf));

            } else if (random_choice == 1) {
                // Handle powermove
                int power_attack = rand() % 19 + 12; // Example power move damage
                // HANDLE IF POWERMOVE LANDS OR NOT
                write(attacker->fd, "Random Choice chose powermove\r\n",
                strlen("Random Choice chose powermove\r\n"));
                
                i % 2 == 0 ? (p1_pm--) : (p2_pm--);
                i % 2 == 0 ? (p2_hp -= power_attack) : (p1_hp -= power_attack);
                
                sprintf(buf, "You hit %s for %d damage!\r\n", defender->name, power_attack);
                write(attacker->fd, buf, strlen(buf));

                sprintf(buf, "%s hits you for %d damage!\r\n", attacker->name, power_attack);
                write(defender->fd, buf, strlen(buf));
            }
        } else {
            // Not among the input that is available.
            i++; //Same turn. Ask again
        }

        // Increment turn counter
        i++;
    }

    if (p1_hp <= 0 || p2_hp <= 0) {
        char loss_message[MAX_BUF]; // Buffer for loss message
        if (p1_hp <= 0) {
            sprintf(loss_message, "You are no match for %s. You scurry away...\r\n\r\nAwaiting next opponent...\r\n", p2->name);
            // Send the loss message to the player whose HP reached 0
            write(p1->fd, loss_message, strlen(loss_message));
            sprintf(loss_message, "%s gives up. You win!\r\n\r\nAwaiting next opponent...\r\n", p1->name);
            write(p2->fd, loss_message, strlen(loss_message)); //Misleading variable name. loss message is actually victory message.
        } else {
            sprintf(loss_message, "You are no match for %s. You scurry away...\r\n\r\nAwaiting next opponent...\r\n", p1->name);
            // Send the loss message to the player whose HP reached 0
            write(p2->fd, loss_message, strlen(loss_message));
            sprintf(loss_message, "%s gives up. You win!\r\n\r\nAwaiting next opponent...\r\n", p2->name);
            write(p1->fd, loss_message, strlen(loss_message)); //Misleading variable name. loss message is actually victory message.
        }
    }
    p1->state=WAITING;
    p2->state=WAITING;
    
    return;

}

void delete_client(int fd) {
    if (front != NULL) {
        if (front->client.fd == fd) {
            front = front->next;
            return;
        }
        
        ClientNode *prev = front;
        ClientNode *curr = front->next;
        while (curr->next != NULL) {
            if (curr->client.fd == fd) {
                //Client to be deleted.
                free(curr->client.name);
                free(curr);
                prev->next = curr->next;
                return;
            }
            else {
                curr = curr->next;
            }
        }
        // client to be deleted is the last one in the list.
        free(curr);
        prev->next = NULL;
    }
    return;
}