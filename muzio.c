#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <glib.h>
#include <gst/gst.h>
#include <time.h>

#define CONFIG_FILE "config.txt"
void open_settings_window(GtkWidget *widget, gpointer data);

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
GtkWidget *settings_window;
GtkWidget *status_label;
GtkWidget *play_pause_button;
GtkWidget *change_dir_button;  
GtkWidget *directory_label;
GtkWidget *settings_button;
CircularDoublyLinkedList song_list;
GMutex list_mutex;
GThread *download_thread = NULL;
Node *current_song = NULL;
GstElement *pipeline = NULL;
gint64 current_position = 0;
gboolean is_loop_enabled = FALSE;
gboolean is_shuffle_enabled = FALSE;
char *music_dir = NULL;

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
    snprintf(command, sizeof(command), "yt-dlp -x --audio-format mp3 -f bestaudio --embed-thumbnail --no-warnings --no-check-certificate --hls-prefer-native \"%s\" -o \"%s/%(title)s.%%(ext)s\"", url, music_dir);
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

    gchar *file_path = g_strdup_printf("file://%s/%s", music_dir, song_name);
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

void toggle_loop(GtkWidget *widget, gpointer data) {
    is_loop_enabled = !is_loop_enabled; // Toggle loop state

    GtkWidget *loop_icon;
    if (is_loop_enabled) {
        // Set an "active" loop icon
        loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Looping current song.");
    } else {
        // Set a "normal" loop icon
        loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Looping disabled.");
    }

    // Update the button's icon
    gtk_button_set_image(GTK_BUTTON(widget), loop_icon);
}

// Function to shuffle the playlist
void shuffle_playlist(CircularDoublyLinkedList *list) {
    if (is_empty(list)) return;

    g_mutex_lock(&list_mutex);
    Node *nodes[1000]; // Adjust size as needed for your playlist
    int count = 0;

    Node *temp = list->head;
    do {
        nodes[count++] = temp;
        temp = temp->next;
    } while (temp != list->head);

    srand(time(NULL)); // Seed the random generator

    // Shuffle nodes array
    for (int i = count - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        Node *temp_node = nodes[i];
        nodes[i] = nodes[j];
        nodes[j] = temp_node;
    }

    // Reconnect nodes in the shuffled order
    for (int i = 0; i < count; ++i) {
        nodes[i]->next = nodes[(i + 1) % count];
        nodes[i]->prev = nodes[(i - 1 + count) % count];
    }

    // Update the head of the list to the first node in the shuffled order
    list->head = nodes[0];
    g_mutex_unlock(&list_mutex);

    // If shuffle is enabled, update the current song to the first song in the shuffled list
    current_song = list->head;
    play_song(current_song->song_name);
    gtk_label_set_text(GTK_LABEL(status_label), "Shuffle enabled. Playing first song.");
}

void toggle_shuffle(GtkWidget *widget, gpointer data) {
    is_shuffle_enabled = !is_shuffle_enabled; // Toggle shuffle state

    GtkWidget *shuffle_icon;
    if (is_shuffle_enabled) {
        shuffle_playlist(&song_list);
        shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Shuffling playlist.");
    } else {
        shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Shuffle disabled.");
    }

    gtk_button_set_image(GTK_BUTTON(widget), shuffle_icon);
}

// Modified bus call function to handle loop logic
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            if (is_loop_enabled && current_song) {
                play_song(current_song->song_name); // Replay the current song
            } else {
                play_next_song(); // Play the next song
            }
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

void save_music_directory(const char *dir) {
    FILE *file = fopen(CONFIG_FILE, "w");
    if (file) {
        fprintf(file, "%s\n", dir);
        fclose(file);
    }
}

char *load_music_directory() {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) return NULL;

    char *dir = (char *)malloc(256);
    if (fgets(dir, 256, file)) {
        // Remove newline character at the end, if present
        dir[strcspn(dir, "\n")] = '\0';
    }
    fclose(file);
    return dir;
}
    
void load_songs_from_directory() {
    if (!music_dir) {
        gtk_label_set_text(GTK_LABEL(status_label), "No music directory selected.");
        return;
    }

    struct dirent *entry;
    DIR *dir = opendir(music_dir);
    if (dir == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "Failed to open directory.");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".mp3") != NULL) {
            add_song(&song_list, entry->d_name);
        }
    }

    closedir(dir);
}

void ask_for_music_directory() {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Music Directory", GTK_WINDOW(main_window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Select", GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        const char *selected_dir = gtk_file_chooser_get_filename(chooser);
        music_dir = strdup(selected_dir);
        save_music_directory(music_dir);  // Save the selected directory
        load_songs_from_directory();  // Load songs from the newly selected directory
    }

    gtk_widget_destroy(dialog);
}

void change_music_directory_button(GtkWidget *widget, gpointer data) {
    ask_for_music_directory();
}

void update_directory_label() {
    if (music_dir) {
        gchar *label_text = g_strdup_printf("Directory: %s", music_dir);
        gtk_label_set_text(GTK_LABEL(directory_label), label_text);
        g_free(label_text);
    }
}

void create_settings_ui() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(settings_window), vbox);

    // Directory label
    directory_label = gtk_label_new("Directory: ");
    gtk_box_pack_start(GTK_BOX(vbox), directory_label, FALSE, FALSE, 0);
    update_directory_label();

    // Change directory button
    change_dir_button = gtk_button_new_with_label("Change Directory");
    g_signal_connect(change_dir_button, "clicked", G_CALLBACK(change_music_directory_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), change_dir_button, FALSE, FALSE, 0);

    // URL entry and download button
    url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "Enter the song URL here");
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    GtkWidget *download_button = gtk_button_new_with_label("Download Song");
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), download_button, FALSE, FALSE, 0);
}

void open_settings_window(GtkWidget *widget, gpointer data) {
    if (!settings_window) {
        // Create the settings window only once
        settings_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(settings_window), "Settings");
        g_signal_connect(settings_window, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);

        create_settings_ui();  // Set up the UI for the settings window
    }

    gtk_widget_show_all(settings_window);  // Show the settings window
}

void create_ui() {
    gtk_window_set_default_size(GTK_WINDOW(main_window), 400, 300);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    // Create an hbox for the media control buttons
    GtkWidget *controls_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), controls_hbox, FALSE, FALSE, 0);

    // Previous button
    GtkWidget *previous_button = gtk_button_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(previous_button, "clicked", G_CALLBACK(previous_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), previous_button, TRUE, TRUE, 0);

    // Play/Pause button
    play_pause_button = gtk_button_new();
    GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON); // Start with play icon
    gtk_button_set_image(GTK_BUTTON(play_pause_button), play_icon);
    g_signal_connect(play_pause_button, "clicked", G_CALLBACK(play_pause_button_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), play_pause_button, TRUE, TRUE, 0);

    // Next button
    GtkWidget *next_button = gtk_button_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(next_button, "clicked", G_CALLBACK(next_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), next_button, TRUE, TRUE, 0);

    // Status label at the bottom
    status_label = gtk_label_new("Playing Song...");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

    // Create an hbox for the loop and shuffle buttons
    GtkWidget *extra_controls_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), extra_controls_hbox, FALSE, FALSE, 0);

    // Loop button
    GtkWidget *loop_button = gtk_button_new();
    GtkWidget *loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat", GTK_ICON_SIZE_BUTTON); // Loop icon
    gtk_button_set_image(GTK_BUTTON(loop_button), loop_icon);
    g_signal_connect(loop_button, "clicked", G_CALLBACK(toggle_loop), NULL);
    gtk_box_pack_start(GTK_BOX(extra_controls_hbox), loop_button, TRUE, TRUE, 0);

    // Shuffle button
    GtkWidget *shuffle_button = gtk_button_new();
    GtkWidget *shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle", GTK_ICON_SIZE_BUTTON); // Shuffle icon
    gtk_button_set_image(GTK_BUTTON(shuffle_button), shuffle_icon);
    g_signal_connect(shuffle_button, "clicked", G_CALLBACK(toggle_shuffle), NULL);
    gtk_box_pack_start(GTK_BOX(extra_controls_hbox), shuffle_button, TRUE, TRUE, 0);

    // Settings button with settings icon
    GtkWidget *settings_button = gtk_button_new_from_icon_name("preferences-system", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(settings_button, "clicked", G_CALLBACK(open_settings_window), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), settings_button, FALSE, FALSE, 0);

}

void add_css_style() {
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css_provider, "style.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
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

    // Try loading the saved music directory
    music_dir = load_music_directory();

    // If no saved directory, ask for it
    if (!music_dir) {
        ask_for_music_directory();
    } else {
        load_songs_from_directory();
    }

    pipeline = gst_element_factory_make("playbin", "player");

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, NULL);  // Add bus watch for EOS and error handling
    gst_object_unref(bus);

    create_ui();  // Create the main UI
    add_css_style();  // Apply CSS style

    // Enable shuffle by default
    toggle_shuffle(NULL, NULL); // This will shuffle the playlist

    if (!is_empty(&song_list)) {
        current_song = song_list.head;
        play_song(current_song->song_name);  // Automatically play the first song
    }

    gtk_widget_show_all(main_window);
    gtk_main();

    gst_element_set_state(pipeline, GST_STATE_NULL);  // Cleanup GStreamer
    gst_object_unref(pipeline);
    return 0;
}
