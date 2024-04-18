#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

int url_count = 0;  // Counter for fetched URLs
int url_limit = 100;  // Limit of URLs to fetch
pthread_mutex_t count_mutex;
pthread_cond_t limit_reached_cond;

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


void initQueue(URLQueue *queue) {
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->lock, NULL);
}

void enqueue(URLQueue *queue, const char *url) {
    pthread_mutex_lock(&count_mutex);
    if (url_count >= url_limit) {
        pthread_mutex_unlock(&count_mutex);
        return;  // Stop enqueueing if limit is reached
    }
    pthread_mutex_unlock(&count_mutex);

    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));
    newNode->url = strdup(url);
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

//this runs in its own proccess


void extract_and_enqueue_urls(const char *html, URLQueue *queue) {
    const char *pattern = "<a href=\"";
    const char *ptr = strstr(html, pattern);
    while (ptr != NULL) {
        ptr += strlen(pattern);  // Move past the "<a href=\""
        const char *end = strchr(ptr, '"');
        if (end != NULL) {
            char *url = strndup(ptr, end - ptr);
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
    size_t space_left = data->buffer_size - dest_len - 1; // -1 for NULL terminator

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
    CallbackData data = {malloc(1024 * 100), 1024 * 100};  // Allocate space for the fetched page content.

    while (1) {
        char *url = dequeue(queue);
        if (url == NULL) {
            break;  // Exit the loop if no URLs are left to process.
        }

        pthread_mutex_lock(&count_mutex);
        if (url_count >= url_limit) {
            pthread_mutex_unlock(&count_mutex);
            free(url);
            break;  // Exit if the URL limit is reached.
        }
        url_count++;  // Increment the URL counter safely within the mutex lock.
        pthread_mutex_unlock(&count_mutex);

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
            data.buffer[0] = '\0';  // Clear the buffer before using it to fetch new content.

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                printf("Fetched URL: %s\n", url);  // Print only successfully fetched URLs.
                extract_and_enqueue_urls(data.buffer, queue);
            } else {
                fprintf(stderr, "Error fetching %s: %s\n", url, curl_easy_strerror(res));  // Print errors if the fetch fails.
            }
            free(url);  // Free the URL after processing.
        }
    }

    free(data.buffer);  // Free the buffer allocated for page content.
    curl_easy_cleanup(curl);  // Clean up the CURL instance.
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

    printf("Starting URL: %s with limit %d\n", starting_url, url_limit); // Debug print

    // Further initialization and thread starting...


    // Initialize mutex and condition variable
    pthread_mutex_init(&count_mutex, NULL);
    pthread_cond_init(&limit_reached_cond, NULL);

    // URL queue and threading setup
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

    // Clean up
    pthread_mutex_destroy(&count_mutex);
    pthread_cond_destroy(&limit_reached_cond);

    return 0;
}
