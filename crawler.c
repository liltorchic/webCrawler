#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>

int url_count = 0;
int url_limit = 100;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *fptr;

typedef struct URLQueueNode {
    char *url;
    struct URLQueueNode *next;
} URLQueueNode;

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

void initQueue(URLQueue *queue) {
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->lock, NULL);
}

void enqueue(URLQueue *queue, const char *url) {
    pthread_mutex_lock(&count_mutex);
    if (url_count >= url_limit) {
        pthread_mutex_unlock(&count_mutex);
        return;
    }
    url_count++;
    pthread_mutex_unlock(&count_mutex);

    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));
    if (!newNode) {
        perror("Failed to allocate memory for URL node");
        return;
    }
    size_t len = strlen(url) + 1;
    newNode->url = malloc(len);
    if (newNode->url) {
        memcpy(newNode->url, url, len);
    } else {
        free(newNode);
        perror("Failed to duplicate URL");
        return;
    }
    newNode->next = NULL;

    pthread_mutex_lock(&queue->lock);
    if (queue->tail) {
        queue->tail->next = newNode;
    } else {
        queue->head = newNode;
    }
    queue->tail = newNode;
    pthread_mutex_unlock(&queue->lock);
}

char *dequeue(URLQueue *queue) {
    pthread_mutex_lock(&queue->lock);
    if (!queue->head) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    URLQueueNode *temp = queue->head;
    char *url = temp->url;
    queue->head = queue->head->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    free(temp);
    pthread_mutex_unlock(&queue->lock);
    return url;
}

void extract_and_enqueue_urls_from_file(const char *filename, URLQueue *queue) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open file for URL extraction");
        return;
    }

    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;
    while (!feof(fp)) {
        if (cap == 0) {
            cap = 256;
            line = malloc(cap);
            if (!line) break;
        }
        if (fgets(line, cap, fp) == NULL) break;

        // Ensure the whole line is read
        while (strchr(line, '\n') == NULL && !feof(fp)) {
            cap *= 2;
            char *new_line = realloc(line, cap);
            if (!new_line) {
                free(line);
                line = NULL;
                break;
            }
            line = new_line;
            if (fgets(line + strlen(line), cap - strlen(line), fp) == NULL) break;
        }

        const char *pattern = "<a href=\"";
        char *ptr = strstr(line, pattern);
        while (ptr) {
            ptr += strlen(pattern);
            char *end = strchr(ptr, '"');
            if (end) {
                size_t url_len = end - ptr;
                char *url = malloc(url_len + 1);
                if (url) {
                    memcpy(url, ptr, url_len);
                    url[url_len] = '\0';
                    enqueue(queue, url);
                    free(url);
                }
            }
            ptr = strstr(end ? end + 1 : ptr, pattern);
        }
    }
    free(line);
    fclose(fp);
    remove(filename);  // Delete the file after processing
}

void *fetch_url(void *arg) {
    URLQueue *queue = (URLQueue *)arg;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        return NULL;
    }

    while (1) {
        char *url = dequeue(queue);
        if (!url) {
            break;
        }

        char filename[256];
        sprintf(filename, "output_%d.html", url_count);
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            fprintf(stderr, "Failed to open file for URL: %s\n", url);
            free(url);
            continue;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            printf("Fetched URL: %s\n", url);
            fprintf(fptr, "Fetched URL: %s\n", url);
        } else {
            fprintf(stderr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
            fprintf(fptr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
        }

        fclose(fp);
        extract_and_enqueue_urls_from_file(filename, queue);

        free(url);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <starting-url> <url-limit>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *starting_url = argv[1];
    url_limit = atoi(argv[2]);
    if (url_limit <= 0) {
        fprintf(stderr, "Invalid URL limit. Please enter a positive integer.\n");
        return EXIT_FAILURE;
    }

    printf("Starting URL: %s with limit %d\n", starting_url, url_limit);

    fptr = fopen("log.txt", "w");
    if (!fptr) {
        perror("Failed to open log file");
        return EXIT_FAILURE;
    }

    URLQueue queue;
    initQueue(&queue);
    enqueue(&queue, starting_url);

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, fetch_url, &queue);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.lock);
    pthread_mutex_destroy(&count_mutex);
    fclose(fptr);

    return EXIT_SUCCESS;
}
