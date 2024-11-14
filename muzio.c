#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <glib.h>
#include <gst/gst.h>
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
GtkWidget *play_pause_button; // The play/pause button
CircularDoublyLinkedList song_list;
GMutex list_mutex;
GThread *download_thread = NULL;
Node *current_song = NULL;
GstElement *pipeline = NULL;

gint64 current_position = 0; // Variable to store current position

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
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
}

void play_song(const char *song_name) {
    stop_current_song();

    gchar *file_path = g_strdup_printf("file://%s/%s", MUSIC_DIR, song_name);
    g_object_set(G_OBJECT(pipeline), "uri", file_path, NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    gtk_label_set_text(GTK_LABEL(status_label), "Playing Song...");

    // Change play/pause button icon to "media-playback-pause"
    GtkWidget *pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);

    // Reset current_position to 0 when starting a new song
    current_position = 0;

    g_free(file_path);
}

void pause_song() {
    if (pipeline) {
        // Get the current playback position before pausing
        gst_element_query_position(pipeline, GST_FORMAT_TIME, &current_position);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        // Change play/pause button icon to "media-playback-start"
        GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(play_pause_button), play_icon);
        gtk_label_set_text(GTK_LABEL(status_label), "Song Paused");
    }
}

void resume_song() {
    if (pipeline) {
        gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_SET, current_position, GST_SEEK_TYPE_NONE, 0);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        
        // Change play/pause button icon to "media-playback-pause"
        GtkWidget *pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);
        gtk_label_set_text(GTK_LABEL(status_label), "Resuming Song...");
    }
}

void play_pause_button_toggled(GtkWidget *widget, gpointer data) {
    GstState current_state;
    gst_element_get_state(pipeline, &current_state, NULL, GST_CLOCK_TIME_NONE);

    if (current_state == GST_STATE_PLAYING) {
        pause_song(); // Pause the song if it's currently playing
    } else {
        resume_song(); // Resume the song if it's paused
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

void next_song_button(GtkWidget *widget, gpointer data) {
    play_next_song();
}

void play_previous_song() {
    if (current_song && current_song->prev) {
        current_song = current_song->prev;
        play_song(current_song->song_name);
        gtk_label_set_text(GTK_LABEL(status_label), "Playing Previous Song...");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "No previous song to play.");
    }
}

void previous_song_button(GtkWidget *widget, gpointer data) {
    play_previous_song();
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            play_next_song();
            break;
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_free(debug);
            g_error_free(err);
            gtk_label_set_text(GTK_LABEL(status_label), "Error occurred while playing song.");
            break;
        }
        default:
            break;
    }
    return TRUE;
}

void create_ui() {
    gtk_window_set_default_size(GTK_WINDOW(main_window), 400, 300);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "Enter the song URL here");
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    GtkWidget *download_button = gtk_button_new_with_label("Download Song");
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), download_button, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    GtkWidget *previous_button = gtk_button_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(previous_button, "clicked", G_CALLBACK(previous_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), previous_button, TRUE, TRUE, 0);

    // Play/Pause Button
    play_pause_button = gtk_button_new();
    GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON); // Start with play icon
    gtk_button_set_image(GTK_BUTTON(play_pause_button), play_icon);
    g_signal_connect(play_pause_button, "clicked", G_CALLBACK(play_pause_button_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), play_pause_button, TRUE, TRUE, 0);

    GtkWidget *next_button = gtk_button_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(next_button, "clicked", G_CALLBACK(next_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), next_button, TRUE, TRUE, 0);

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
    gst_init(&argc, &argv);
    init_list(&song_list);
    g_mutex_init(&list_mutex);

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Muzio");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 300, 200);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    pipeline = gst_element_factory_make("playbin", "player");

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, NULL);  // Add bus watch for EOS and error handling
    gst_object_unref(bus);

    create_ui();
    add_css_style();
    load_songs_from_directory();

    if (!is_empty(&song_list)) {
        current_song = song_list.head;
    }

    gtk_widget_show_all(main_window);
    gtk_main();

    gst_element_set_state(pipeline, GST_STATE_NULL);  // Cleanup GStreamer
    gst_object_unref(pipeline);
    return 0;
} 