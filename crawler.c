#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

int url_count = 0;
int url_limit = 100;
pthread_mutex_t count_mutex;
FILE *fptr;

typedef struct URLQueueNode {
    char *url;
    struct URLQueueNode *next;
} URLQueueNode;

typedef struct {
    URLQueueNode *head, *tail;
    pthread_mutex_t lock;
} URLQueue;

typedef struct {
    char *buffer;
    size_t buffer_size;
} CallbackData;

void initQueue(URLQueue *queue);
void enqueue(URLQueue *queue, const char *url);
char *dequeue(URLQueue *queue);
void extract_and_enqueue_urls(const char *html, URLQueue *queue);
size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream);
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
    pthread_mutex_unlock(&count_mutex);

    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));
    if (newNode == NULL) {
        return;
    }
    newNode->url = strdup(url);  // Consider replacing strdup with a manual copy to ensure compliance
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
    if (queue->head == NULL) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    URLQueueNode *temp = queue->head;
    char *url = temp->url;
    queue->head = queue->head->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    free(temp);
    pthread_mutex_unlock(&queue->lock);
    return url;
}

void extract_and_enqueue_urls(const char *html, URLQueue *queue) {
    const char *pattern = "<a href=\"";
    const char *ptr = strstr(html, pattern);
    while (ptr != NULL) {
        ptr += strlen(pattern);
        const char *end = strchr(ptr, '"');
        if (end != NULL) {
            char *url = strndup(ptr, end - ptr);  // Consider replacing strndup with a manual copy to ensure compliance
            enqueue(queue, url);
            free(url);
            ptr = strstr(end, pattern);
        }
    }
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
    CallbackData *data = (CallbackData *)stream;
    size_t real_size = size * nmemb;
    size_t dest_len = strlen(data->buffer);
    size_t space_left = data->buffer_size - dest_len - 1;

    if (space_left > real_size) {
        strncat(data->buffer, (char*)ptr, real_size);
    } else {
        strncat(data->buffer, (char*)ptr, space_left);
    }
    return real_size;
}

void *fetch_url(void *arg) {
    URLQueue *queue = (URLQueue *)arg;
    CURL *curl = curl_easy_init();
    CallbackData data = {malloc(1024 * 100), 1024 * 100};

    if (!data.buffer) {  // Check for malloc failure
        return NULL;
    }

    while (1) {
        char *url = dequeue(queue);
        if (url == NULL) {
            break;
        }

        pthread_mutex_lock(&count_mutex);
        if (url_count >= url_limit) {
            pthread_mutex_unlock(&count_mutex);
            free(url);
            break;
        }
        url_count++;
        pthread_mutex_unlock(&count_mutex);

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
            data.buffer[0] = '\0';

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                printf("Fetched URL: %s\n", url);
                fprintf(fptr, "Fetched URL: %s\n", url);
                extract_and_enqueue_urls(data.buffer, queue);
            } else {
                fprintf(stderr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
                fprintf(fptr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));
            }
            free(url);
        }
    }

    free(data.buffer);
    curl_easy_cleanup(curl);
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("Usage: %s <starting-url> <url-limit>\n", argv[0]);
        return 1;
    }

    char *starting_url = argv[1];
    url_limit = atoi(argv[2]);
    if (url_limit <= 0) {
        fprintf(stderr, "Invalid URL limit. Please enter a positive integer.\n");
        return 1;
    }

    printf("Starting URL: %s with limit %d\n", starting_url, url_limit);

    fptr = fopen("log.txt", "w");
    if (!fptr) {
        fprintf(stderr, "Failed to open log file.\n");
        return 1;
    }

    pthread_mutex_init(&count_mutex, NULL);

    URLQueue queue;
    initQueue(&queue);
    enqueue(&queue, starting_url);

    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, fetch_url, (void *)&queue);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&count_mutex);
    fclose(fptr);

    return 0;
}
