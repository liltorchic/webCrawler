#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>

size_t url_count = 0;
size_t url_limit = 100;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *fptr;

/*
* Struct URLQueueNode
* A Node struct to be used in a queue
*/
typedef struct URLQueueNode {
    char *url;
    struct URLQueueNode *next;
} URLQueueNode;

/*
* Struct URLQueue
* A queue struct of urls
*/
typedef struct {
    URLQueueNode *head, *tail;
    pthread_mutex_t lock;
} URLQueue;

void initQueue(URLQueue *queue);
void enqueue(URLQueue *queue, const char *url);
char *dequeue(URLQueue *queue);
void extract_and_enqueue_urls_from_file(const char *filename, URLQueue *queue);
void *fetch_url(void *arg);
int main(int argc, char *argv[]);

/*
* initilises queue data type for storing urls
* URLQueue: the queue to initilize
*/
void initQueue(URLQueue *queue) {
    queue->head = queue->tail = NULL; //init head and tail to null
    pthread_mutex_init(&queue->lock, NULL);  //make sure mutex is unlocked on queue
}

/*
* enqueues a new url onto our queue
* *queue: the queue to enqueue into
* *url: the url to enqueue
*/
void enqueue(URLQueue *queue, const char *url) {

////////
//Init

    pthread_mutex_lock(&count_mutex);//lock thread

    //check to see if we are under our url limit
    if (url_count >= url_limit) {
        pthread_mutex_unlock(&count_mutex);
        return;
    }

    url_count++;//increase count of urls visited

    pthread_mutex_unlock(&count_mutex);

    //allocate memory for a new url node
    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));

    //if allocating our queue node failed
    if (!newNode) {
        perror("Failed to allocate memory for URL node");
        return;
    }

    size_t len = strlen(url) + 1;//get char length of url

    newNode->url = malloc(len); //allocate memory for our url in our node

    //if our new node is valid
    if (newNode->url) {
        memcpy(newNode->url, url, len);//copy url into new node struct
    } else {
        free(newNode);//free memory
        perror("Failed to duplicate URL");
        return;
    }

    newNode->next = NULL;//init next in newnode

////////
// logic

    pthread_mutex_lock(&queue->lock);//lock queue thread
    
    //if our queue has a tail (if our queue isnt empty)
    if (queue->tail) {
        queue->tail->next = newNode;//add a pointer from prev item to our node
    } else {//if our queue is empty
        queue->head = newNode; //set our node as the head
    }

    queue->tail = newNode;//add our node to the queue

    pthread_mutex_unlock(&queue->lock); //unlock thread
}

/*
* removes a url from our queue
* *queue: the queue to dequeue from
* Returns: the url from the dequeued node 
*/
char *dequeue(URLQueue *queue) {

    pthread_mutex_lock(&queue->lock); // lock this thread

    //if the head of the queue is empty
    if (!queue->head) {
        pthread_mutex_unlock(&queue->lock);//unlock
        return NULL;
    }

    URLQueueNode *temp = queue->head;//create pointer to head
    char *url = temp->url;//create pointer to url

    queue->head = queue->head->next;//remove pointer from queue

    //if the head is empty
    if (!queue->head) {
        queue->tail = NULL;//set tail to be null
    }

    free(temp);//frees memory
    pthread_mutex_unlock(&queue->lock);//unlock thread

    return url;
}

/*
* extract urls from crawled site
* *filename: name of file to grab urls
* *queue: queue to enqueue urls 
*/
void extract_and_enqueue_urls_from_file(const char *filename, URLQueue *queue) {

    //open file pointer
    FILE *fp = fopen(filename, "r");

    //if file opening failed
    if (!fp) {
        perror("Failed to open file for URL extraction");
        return;
    }

    char *line = NULL;//init variable for tokenized line
    size_t cap = 0;//dynamically sized buffer

    //while we are not at the end of file
    while (!feof(fp)) 
    {

        if (cap == 0) 
        {
            cap = 256;
            line = malloc(cap);
            if (!line) break;
        }

        //if the file is empty
        if (fgets(line, (int)cap, fp) == NULL) break;

       //read each line
        while (strchr(line, '\n') == NULL && !feof(fp)) {
            cap *= 2;//double capacity of buffer
            char *new_line = realloc(line, cap);//read the line

            //if the new line is null
            if (!new_line) { 
                free(line);//free memory
                line = NULL;//set the line to null
                break;
            }

            line = new_line;
            if (fgets(line + strlen(line), cap - strlen(line), fp) == NULL) break;//if the next line is null, break
        }
    ////////
    //detecting urls

        const char *pattern = "<a href=\""; // starting pattern of links in html

        char *ptr = strstr(line, pattern);//Pointer to the first character of the found pattern in line

        //if the pattern is found (not null)
        while (ptr) 
        {
            ptr += strlen(pattern);//go to end of the pattern

            char *end = strchr(ptr, '"');//returns pointer to first char "

            //if the pointer is valid
            if (end) 
            {
                size_t url_len = end - ptr; 

                char *url = malloc(url_len + 1);//allocates size for url

                //if url is available in memory
                if (url) 
                {
                    memcpy(url, ptr, url_len);//copy url
                    url[url_len] = '\0';//add nessesary formatting
                    enqueue(queue, url);//enque url onto queue!!!
                    free(url);///free reserved memory
                }
            }

            ptr = strstr(end ? end + 1 : ptr, pattern);
        }
    }

    free(line);// Frees memory

    fclose(fp); // close the file

    remove(filename);  // Delete the file after processing
}

/*
* crawl url for file
*
*/
void *fetch_url(void *arg) {

    URLQueue *queue = (URLQueue *)arg;

    CURL *curl = curl_easy_init();//init curl library

    //check to load curl
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        return NULL;
    }

    while (1) 
    {

        // a url from the queue
        char *url = dequeue(queue);

        if (!url) {
            break;
        }

        char filename[256];

        //create empty file for storing html
        sprintf(filename, "output_%zu.html", url_count);

        //open file for writing
        FILE *fp = fopen(filename, "wb");

        //check for file pointer
        if (!fp) {
            fprintf(stderr, "Failed to open file for URL: %s\n", url);
            free(url);
            continue;
        }

        //curl
        //gets and writes html from site to file
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);

        //curl return codes
        CURLcode res = curl_easy_perform(curl);

        //logic for curl result
        if (res == CURLE_OK) {
            printf("Fetched URL: %s\n", url);
            fprintf(fptr, "Fetched URL: %s\n", url);
        } else {
            fprintf(stderr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
            fprintf(fptr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
        }

        fclose(fp);//close pointer

        extract_and_enqueue_urls_from_file(filename, queue);//search for more urls from crawled site

        free(url);//free memory
    }

    curl_easy_cleanup(curl);//shutdown curl

    return NULL;
}

/*
* main function
*/
int main(int argc, char *argv[]) {

////////////////////////////////////
////INIT 

    // initial check for 2 arguments
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <starting-url> <url-limit>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *starting_url = argv[1];// grab pointer from argv 1
    url_limit = atoi(argv[2]);//grab int limit from arg 2

    //check to make sure url limit is at least 1
    if (url_limit <= 0) {
        fprintf(stderr, "Invalid URL limit. Please enter a positive integer.\n");
        return EXIT_FAILURE;
    }

    printf("Starting URL: %s with limit %zu\n", starting_url, url_limit);


    fptr = fopen("log.txt", "w"); //syscall to open log file

    //if error opening file
    if (!fptr) {
        perror("Failed to open log file");
        return EXIT_FAILURE;
    }

////////////////////////////////////
////MAIN 

    URLQueue queue;//create queue
    initQueue(&queue);//init queue
    enqueue(&queue, starting_url);//enqueue first url into queue

    // create 4 new threads
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, fetch_url, &queue);
    }

////////////////////////////////////
////SHUTDOWN

    //rejoin threads to main thread
    for (int i = 0; i < 4; i++) {
        //get thread from sys using thread_id, exit_status_pointer
        pthread_join(threads[i], NULL);
    }

    //destroys (uninitilizes) mutex object
    pthread_mutex_destroy(&queue.lock);//removes thread lock from queue
    pthread_mutex_destroy(&count_mutex);//removes thread lock 

    fclose(fptr);//close log pointer

    return EXIT_SUCCESS;
}
