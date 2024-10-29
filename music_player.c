#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h> // Include GLib for GThread
#include <time.h> 

#define MUSIC_DIR "/home/msgs/Music"
#define QUEUE_CAPACITY 100  // Set a reasonable capacity for the circular queue

typedef struct CircularQueue {
    char **items; // Array of song names
    int front;
    int rear;
    int capacity;
} CircularQueue;

GtkWidget *url_entry;
GtkWidget *main_window;
CircularQueue song_queue; // Declare a circular queue for songs
GMutex queue_mutex;       // Mutex to protect access to the queue
GThread *download_thread = NULL; // Thread for downloading songs

// Function to initialize the circular queue
void init_queue(CircularQueue* queue) {
    queue->capacity = QUEUE_CAPACITY;
    queue->items = (char**)malloc(queue->capacity * sizeof(char*));
    queue->front = 0;
    queue->rear = 0;
}

// Check if the queue is full
int is_full(CircularQueue* queue) {
    return (queue->rear + 1) % queue->capacity == queue->front;
}

// Check if the queue is empty
int is_empty(CircularQueue* queue) {
    return queue->front == queue->rear;
}

// Enqueue a song name
void enqueue(CircularQueue* queue, const char* song_name) {
    g_mutex_lock(&queue_mutex);  // Lock the mutex
    if (is_full(queue)) {
        printf("Queue is full!\n");
        g_mutex_unlock(&queue_mutex);  // Unlock the mutex
        return;
    }
    queue->items[queue->rear] = strdup(song_name); // Duplicate the string
    queue->rear = (queue->rear + 1) % queue->capacity;
    g_mutex_unlock(&queue_mutex);  // Unlock the mutex
}

// Dequeue a song name
const char* dequeue(CircularQueue* queue) {
    g_mutex_lock(&queue_mutex);  // Lock the mutex
    if (is_empty(queue)) {
        printf("Queue is empty!\n");
        g_mutex_unlock(&queue_mutex);  // Unlock the mutex
        return NULL;
    }
    const char* dequeued_item = queue->items[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    g_mutex_unlock(&queue_mutex);  // Unlock the mutex
    return dequeued_item;
}

// Function to download the song
void *download_song_thread(void *data) {
    const char *url = (const char *)data; // Get the URL from the passed data
    char command[512];

    // Prepare the yt-dlp command with optimizations
    snprintf(command, sizeof(command), "yt-dlp -x --audio-format mp3 -f bestaudio --embed-thumbnail --no-warnings --no-check-certificate --hls-prefer-native \"%s\" -o \"%s/%(title)s.%%(ext)s\"", url, MUSIC_DIR);
    system(command);
    
    // After downloading, enqueue the new song
    char *song_name = strrchr(url, '/') + 1; // Extract the song name from URL
    enqueue(&song_queue, song_name); // Enqueue the song name

    // Free the URL string after usage
    free((void *)data);
    return NULL;
}

// Function called when download button is clicked
void download_song(GtkWidget *widget, gpointer data) {
    const char *url = gtk_entry_get_text(GTK_ENTRY(url_entry));
    if (strlen(url) == 0) {
        g_print("No URL provided.\n");
        return;
    }

    // Duplicate the URL string to pass to the thread
    char *url_copy = strdup(url);
    
    // Create a new thread to download the song
    download_thread = g_thread_new("download_thread", download_song_thread, url_copy);
    
    gtk_entry_set_text(GTK_ENTRY(url_entry), ""); // Clear entry
}

// Function to shuffle the songs
void shuffle_songs() {
    DIR *dir;
    struct dirent *ent;

    // Clear previous queue
    while (!is_empty(&song_queue)) {
        free((char *)dequeue(&song_queue)); // Free the dequeued item
    }

    // Open the music directory
    if ((dir = opendir(MUSIC_DIR)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            // Check if it is an mp3 file
            if (strstr(ent->d_name, ".mp3")) {
                // Add song to queue
                enqueue(&song_queue, ent->d_name);
            }
        }
        closedir(dir);
    }

    // Shuffle the song queue using Fisher-Yates shuffle
    srand(time(NULL)); // Seed the random number generator
    int count = (song_queue.rear - song_queue.front + song_queue.capacity) % song_queue.capacity;

    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1); // Generate a random index
        // Swap the songs at indices i and j
        char *temp = song_queue.items[(song_queue.front + i) % song_queue.capacity];
        song_queue.items[(song_queue.front + i) % song_queue.capacity] = song_queue.items[(song_queue.front + j) % song_queue.capacity];
        song_queue.items[(song_queue.front + j) % song_queue.capacity] = temp;
    }
}

// Function to play the song using popen (hide VLC window)
void play_song(const char *song_name) {
    char command[512];
    snprintf(command, sizeof(command), "cvlc --play-and-exit \"%s/%s\"", MUSIC_DIR, song_name); // Use cvlc for CLI VLC

    // Open the command for reading
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Failed to run VLC");
        return;
    }

    // Wait for the VLC process to finish
    int status = pclose(fp);
    if (status == -1) {
        perror("Failed to close the VLC process");
    }
}

// Function to handle play button click
void play_songs(GtkWidget *widget, gpointer data) {
    shuffle_songs();

    while (!is_empty(&song_queue)) {
        const char *song_name = dequeue(&song_queue);
        if (song_name) {
            play_song(song_name);  // Play the song
            free((char *)song_name); // Free the song name after playing
        }
    }
}

// Function to handle the main window destruction
void on_main_window_destroy(GtkWidget *widget, gpointer data) {
    if (download_thread) {
        g_thread_join(download_thread); // Wait for the download thread to finish
        download_thread = NULL; // Reset the thread reference
    }
    gtk_main_quit(); // Exit the GTK main loop
}

// Function to create the main window
void create_main_window() {
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Muzio");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 400, 300);
    gtk_window_set_resizable(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_position(GTK_WINDOW(main_window), GTK_WIN_POS_CENTER);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    GtkWidget *title_label = gtk_label_new("Welcome to Muzio");
    gtk_widget_set_name(title_label, "title_label");
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);

    url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "Enter song URL...");
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    GtkWidget *download_button = gtk_button_new_with_label("Download Song");
    gtk_widget_set_name(download_button, "download_button");
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_song), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), download_button, FALSE, FALSE, 0);

    GtkWidget *play_button = gtk_button_new_with_label("Play Songs");
    gtk_widget_set_name(play_button, "play_button");
    g_signal_connect(play_button, "clicked", G_CALLBACK(play_songs), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), play_button, FALSE, FALSE, 0);

    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
    gtk_widget_show_all(main_window);

    // CSS Styling
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, 
        "#title_label { font-size: 24px; font-weight: bold; color: #4A90E2; }"
        "#download_button { background-color: #4CAF50; color: white; }"
        "#play_button { background-color: #2196F3; color: white; }"
        "GtkEntry { border: 1px solid #4A90E2; padding: 5px; }"
        "GtkButton:hover { background-color: #007BFF; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), 
        GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    g_mutex_init(&queue_mutex); // Initialize the mutex
    init_queue(&song_queue); // Initialize the circular queue
    create_main_window();
    gtk_main();
    
    // Free allocated memory
    for (int i = 0; i < song_queue.capacity; i++) {
        free(song_queue.items[i]);
    }
    free(song_queue.items);
    g_mutex_clear(&queue_mutex); // Clear the mutex
    return 0;
}
