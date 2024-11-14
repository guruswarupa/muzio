#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <glib.h>
#include <time.h>

#define MUSIC_DIR "/home/msgs/hdd/msgs/Music"

typedef struct Node {
    char *song_name;
    struct Node *next;
    struct Node *prev;
} Node;

typedef struct CircularDoublyLinkedList {
    Node *head;
} CircularDoublyLinkedList;

GtkWidget *url_entry;
GtkWidget *main_window;
GtkWidget *status_label;
CircularDoublyLinkedList song_list;
GMutex list_mutex;
GThread *download_thread = NULL;
Node *current_song = NULL;

void init_list(CircularDoublyLinkedList *list) {
    list->head = NULL;
}

int is_empty(CircularDoublyLinkedList *list) {
    return list->head == NULL;
}

void add_song(CircularDoublyLinkedList *list, const char *song_name) {
    g_mutex_lock(&list_mutex);
    Node *new_node = (Node *)malloc(sizeof(Node));
    new_node->song_name = strdup(song_name);

    if (is_empty(list)) {
        new_node->next = new_node;
        new_node->prev = new_node;
        list->head = new_node;
    } else {
        Node *tail = list->head->prev;
        tail->next = new_node;
        new_node->prev = tail;
        new_node->next = list->head;
        list->head->prev = new_node;
    }
    g_mutex_unlock(&list_mutex);
}

void *download_song_thread(void *data) {
    const char *url = (const char *)data;
    char command[512];
    snprintf(command, sizeof(command), "yt-dlp -x --audio-format mp3 -f bestaudio --embed-thumbnail --no-warnings --no-check-certificate --hls-prefer-native \"%s\" -o \"%s/%(title)s.%%(ext)s\"", url, MUSIC_DIR);
    system(command);

    gtk_label_set_text(GTK_LABEL(status_label), "Download Complete");
    char *song_name = strrchr(url, '/') + 1;
    add_song(&song_list, song_name);

    free((void *)data);
    return NULL;
}

void download_song_button(GtkWidget *widget, gpointer data) {
    const char *url = gtk_entry_get_text(GTK_ENTRY(url_entry));
    if (strlen(url) == 0) {
        gtk_label_set_text(GTK_LABEL(status_label), "No URL provided.");
        return;
    }

    char *url_copy = strdup(url);
    download_thread = g_thread_new("download_thread", download_song_thread, url_copy);
    gtk_entry_set_text(GTK_ENTRY(url_entry), "");
    gtk_label_set_text(GTK_LABEL(status_label), "Downloading...");
}

void stop_current_song() {
    system("pkill vlc");
}

void *play_song_thread(void *data) {
    char *song_name = (char *)data;
    stop_current_song();

    char command[512];
    snprintf(command, sizeof(command), "cvlc --play-and-exit \"%s/%s\"", MUSIC_DIR, song_name);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Failed to run VLC");
        return NULL;
    }
    int status = pclose(fp);
    if (status == -1) {
        perror("Failed to close the VLC process");
    }

    if (current_song) {
        current_song = current_song->next;
    } else {
        current_song = song_list.head;
    }
    return NULL;
}

void play_song(const char *song_name) {
    g_thread_new("play_song_thread", play_song_thread, strdup(song_name));
}

void play_song_button(GtkWidget *widget, gpointer data) {
    if (current_song == NULL) {
        current_song = song_list.head;
    }

    if (current_song) {
        play_song(current_song->song_name);
        gtk_label_set_text(GTK_LABEL(status_label), "Playing Song...");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "No song to play.");
    }
}

void play_next_song() {
    if (current_song && current_song->next) {
        current_song = current_song->next;
        play_song(current_song->song_name);
        gtk_label_set_text(GTK_LABEL(status_label), "Playing Next Song...");
    } else if (current_song) {
        current_song = song_list.head; 
        play_song(current_song->song_name);
        gtk_label_set_text(GTK_LABEL(status_label), "Playing First Song (Looping)...");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "No next song to play.");
    }
}

void *play_next_song_thread(void *data) {
    play_next_song();
    return NULL;
}

void next_song_button(GtkWidget *widget, gpointer data) {
    g_thread_new("next_song_thread", play_next_song_thread, NULL);
}

void play_previous_song() {
    if (current_song && current_song->prev->prev) {
        current_song = current_song->prev->prev;
    } else if (current_song) {
        current_song = song_list.head->prev; // Loop to last song
    }

    if (current_song) {
        play_song(current_song->song_name);
        gtk_label_set_text(GTK_LABEL(status_label), "Playing Previous Song...");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "No previous song to play.");
    }
}

void *play_previous_song_thread(void *data) {
    play_previous_song();
    return NULL;
}

void previous_song_button(GtkWidget *widget, gpointer data) {
    g_thread_new("previous_song_thread", play_previous_song_thread, NULL);
}

void create_ui() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    url_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    GtkWidget *download_button = gtk_button_new_with_label("Download Song");
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), download_button, FALSE, FALSE, 0);

    GtkWidget *play_button = gtk_button_new_with_label("Play Song");
    g_signal_connect(play_button, "clicked", G_CALLBACK(play_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), play_button, FALSE, FALSE, 0);

    GtkWidget *next_button = gtk_button_new_with_label("Next Song");
    g_signal_connect(next_button, "clicked", G_CALLBACK(next_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), next_button, FALSE, FALSE, 0);

    GtkWidget *previous_button = gtk_button_new_with_label("Previous Song");
    g_signal_connect(previous_button, "clicked", G_CALLBACK(previous_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), previous_button, FALSE, FALSE, 0);

    status_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);
}

void add_css_style() {
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css_provider, "style.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
}

void load_songs_from_directory() {
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(MUSIC_DIR)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, ".mp3")) {
                add_song(&song_list, ent->d_name);
            }
        }
        closedir(dir);
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "Error opening music directory.");
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    init_list(&song_list);
    g_mutex_init(&list_mutex);

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Muzio");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 300, 300);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    create_ui();
    add_css_style();
    load_songs_from_directory();

    if (!is_empty(&song_list)) {
        current_song = song_list.head;
    }

    gtk_widget_show_all(main_window);
    gtk_main();
    return 0;
}
