#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <glib.h>
#include <gst/gst.h>
#include <time.h>

#define CONFIG_FILE "config.txt"
#define PLAYLISTS_DIR "playlists"

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
GtkWidget *volume_slider;
GtkWidget *volume_percentage_label;
GtkWidget *seek_scale;
GtkWidget *current_time_label;
GtkWidget *total_time_label;
GstElement *pipeline;
GtkComboBoxText *playlist_combo_box;
GtkWidget *add_to_playlist_button;
CircularDoublyLinkedList song_list;
GMutex list_mutex;
GThread *download_thread = NULL;
Node *current_song = NULL;
GstElement *pipeline = NULL;
gint64 current_position = 0;
gboolean is_loop_enabled = FALSE;
gboolean is_shuffle_enabled = FALSE;
char *music_dir = NULL;
gboolean pipeline_is_playing = FALSE;

void init_list(CircularDoublyLinkedList *list);
int is_empty(CircularDoublyLinkedList *list);
void add_song(CircularDoublyLinkedList *list, const char *song_name);
void *download_song_thread(void *data);
void download_song_button(GtkWidget *widget, gpointer data);
void stop_current_song();
void play_song(const char *song_name);
void pause_song();
void resume_song();
void play_pause_button_toggled(GtkWidget *widget, gpointer data);
void play_next_song();
void next_song_button(GtkWidget *widget, gpointer data);
void play_previous_song();
void previous_song_button(GtkWidget *widget, gpointer data);
void toggle_loop(GtkWidget *widget, gpointer data);
void shuffle_playlist(CircularDoublyLinkedList *list);
void toggle_shuffle(GtkWidget *widget, gpointer data);
void on_volume_changed(GtkRange *range, gpointer data);
static void update_seek_bar_position_thread();
static void start_seek_bar_update_thread();
void on_seek_changed(GtkRange *range, gpointer data);
void reset_seek_scale();
void create_playlist(const char *playlist_name);
void add_song_to_playlist(const char *song_name, const char *playlist_name);
void load_playlists();
void on_add_to_playlist_button_clicked(GtkWidget *widget, gpointer data);
void on_create_playlist_button_clicked(GtkWidget *widget, gpointer data);
void on_play_playlist_button_clicked(GtkWidget *widget, gpointer data);
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
void save_music_directory(const char *dir);
char *load_music_directory();
void load_songs_from_directory();
void ask_for_music_directory();
void change_music_directory_button(GtkWidget *widget, gpointer data);
void update_directory_label();
void create_settings_ui();
void open_settings_window(GtkWidget *widget, gpointer data);
void create_ui();
void add_css_style();
int main(int argc, char *argv[]);

void init_list(CircularDoublyLinkedList *list) {
    list->head = NULL;
}

int is_empty(CircularDoublyLinkedList *list) {
    return list->head == NULL;
}

void add_song(CircularDoublyLinkedList *list, const char *song_name) {
    Node *new_node = (Node *)malloc(sizeof(Node));
    new_node->song_name = strdup(song_name);

    g_mutex_lock(&list_mutex); 

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

    char *url_copy = g_strdup(url); 
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
    start_seek_bar_update_thread();
    reset_seek_scale();
    stop_current_song();

    gchar *file_path = g_strdup_printf("file://%s/%s", music_dir, song_name);
    g_object_set(G_OBJECT(pipeline), "uri", file_path, NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    gtk_label_set_text(GTK_LABEL(status_label), "Playing Song...");

    GtkWidget *pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);

    current_position = 0;
    pipeline_is_playing = TRUE; 
    g_free(file_path);
}

void pause_song() {
    if (pipeline) {
        gst_element_query_position(pipeline, GST_FORMAT_TIME, &current_position);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(play_pause_button), play_icon);
        gtk_label_set_text(GTK_LABEL(status_label), "Song Paused");
    }
}

void resume_song() {
    if (pipeline) {
        gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_SET, current_position, GST_SEEK_TYPE_NONE, 0);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        
        GtkWidget *pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);
        gtk_label_set_text(GTK_LABEL(status_label), "Resuming Song...");
    }
}

void play_pause_button_toggled(GtkWidget *widget, gpointer data) {
    GstState current_state;
    gst_element_get_state(pipeline, &current_state, NULL, GST_CLOCK_TIME_NONE);

    if (current_state == GST_STATE_PLAYING) {
        pause_song(); 
    } else {
        resume_song(); 
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
    is_loop_enabled = !is_loop_enabled; 

    GtkWidget *loop_icon;
    if (is_loop_enabled) {
        loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Looping current song.");
    } else {
        loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Looping disabled.");
    }

}

void shuffle_playlist(CircularDoublyLinkedList *list) {
    if (is_empty(list)) return;

    g_mutex_lock(&list_mutex);
    Node *nodes[1000];
    int count = 0;

    Node *temp = list->head;
    do {
        nodes[count++] = temp;
        temp = temp->next;
    } while (temp != list->head);

    srand(time(NULL));

    for (int i = count - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        Node *temp_node = nodes[i];
        nodes[i] = nodes[j];
        nodes[j] = temp_node;
    }

    for (int i = 0; i < count; ++i) {
        nodes[i]->next = nodes[(i + 1) % count];
        nodes[i]->prev = nodes[(i - 1 + count) % count];
    }

    list->head = nodes[0];
    g_mutex_unlock(&list_mutex);

    current_song = list->head;
    play_song(current_song->song_name);
    gtk_label_set_text(GTK_LABEL(status_label), "Shuffle enabled. Playing first song.");
}

void toggle_shuffle(GtkWidget *widget, gpointer data) {
    is_shuffle_enabled = !is_shuffle_enabled;

    GtkWidget *shuffle_icon;
    if (is_shuffle_enabled) {
        shuffle_playlist(&song_list);
        shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Shuffling playlist.");
    } else {
        shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle", GTK_ICON_SIZE_BUTTON);
        gtk_label_set_text(GTK_LABEL(status_label), "Shuffle disabled.");
    }
}

void on_volume_changed(GtkRange *range, gpointer data) {
    if (pipeline) {
        gdouble value = gtk_range_get_value(range);
        gdouble volume = value / 100.0;
        g_object_set(pipeline, "volume", volume, NULL);

    }
}

static void update_seek_bar_position_thread() {
    while (pipeline_is_playing) {
        if (pipeline) {
            GstFormat format = GST_FORMAT_TIME;
            gint64 current_position = 0;
            gint64 duration = 0;

            if (gst_element_query_position(pipeline, format, &current_position) &&
                gst_element_query_duration(pipeline, format, &duration)) {

                gdouble current_position_sec = (gdouble)current_position / GST_SECOND;
                gdouble duration_sec = (gdouble)duration / GST_SECOND;

                gtk_range_set_value(GTK_RANGE(seek_scale), current_position_sec);
                gtk_range_set_range(GTK_RANGE(seek_scale), 0.0, duration_sec);

                int current_minutes = (int)(current_position_sec / 60);
                int current_seconds = (int)(current_position_sec) % 60;
                gchar *current_time_text = g_strdup_printf("%02d:%02d", current_minutes, current_seconds);
                gtk_label_set_text(GTK_LABEL(current_time_label), current_time_text);
                g_free(current_time_text);

                int total_minutes = (int)(duration_sec / 60);
                int total_seconds = (int)(duration_sec) % 60;
                gchar *total_time_text = g_strdup_printf("%02d:%02d", total_minutes, total_seconds);
                gtk_label_set_text(GTK_LABEL(total_time_label), total_time_text);
                g_free(total_time_text);
            }
        }
        g_usleep(1000000); 
    }
}

static void start_seek_bar_update_thread() {
    g_thread_new("seek-bar-update", (GThreadFunc)update_seek_bar_position_thread, NULL);
}

void on_seek_changed(GtkRange *range, gpointer data) {
    if (pipeline) {
        gdouble seek_position_sec = gtk_range_get_value(GTK_RANGE(seek_scale)); 
        gint64 seek_position_ns = seek_position_sec * 1000000000.0; 
        gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                         GST_SEEK_TYPE_SET, seek_position_ns, GST_SEEK_TYPE_NONE, 0);
    }
}   

void reset_seek_scale() {
    gtk_range_set_value(GTK_RANGE(seek_scale), 0.0);  
}

void create_playlist(const char *playlist_name) {
    if (playlist_name == NULL || playlist_name[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(status_label), "Error: Invalid playlist name.");
        return;
    }

    char playlist_file[512];
    snprintf(playlist_file, sizeof(playlist_file), "%s/%s.txt", PLAYLISTS_DIR, playlist_name);

    FILE *file = fopen(playlist_file, "w");
    if (file) {
        fclose(file);

        if (playlist_combo_box != NULL) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(playlist_combo_box), playlist_name);
        } else {
            gtk_label_set_text(GTK_LABEL(status_label), "Error: Playlist combo box not initialized.");
        }

        char message[256];
        snprintf(message, sizeof(message), "Playlist '%s' created successfully.", playlist_name);
        gtk_label_set_text(GTK_LABEL(status_label), message);
    } else {
        char message[256];
        snprintf(message, sizeof(message), "Error creating playlist file for: %s", playlist_name);
        gtk_label_set_text(GTK_LABEL(status_label), message);
    }
}

void add_song_to_playlist(const char *song_name, const char *playlist_name) {
    char playlist_file[512];
    snprintf(playlist_file, sizeof(playlist_file), "%s/%s.txt", PLAYLISTS_DIR, playlist_name);
    
    FILE *file = fopen(playlist_file, "a");
    if (file) {
        fprintf(file, "%s\n", song_name); 
        fclose(file);
        gtk_label_set_text(GTK_LABEL(status_label), "Song added to playlist.");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "Error adding song to playlist.");
    }
}

void load_playlists() {
    DIR *dir = opendir(PLAYLISTS_DIR);
    if (dir == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "Failed to open playlists directory.");
        return;
    }

    struct dirent *entry;
    gboolean first_playlist_set = FALSE;

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".txt") != NULL) {
            char playlist_name[256];
            strncpy(playlist_name, entry->d_name, sizeof(playlist_name));
            playlist_name[strlen(playlist_name) - 4] = '\0';
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(playlist_combo_box), playlist_name);
            if (!first_playlist_set) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(playlist_combo_box), 0);
                first_playlist_set = TRUE;
            }
        }
    }
    closedir(dir);
}

void on_add_to_playlist_button_clicked(GtkWidget *widget, gpointer data) {
    const char *song_name = current_song->song_name;  
    const char *playlist_name = gtk_combo_box_text_get_active_text(playlist_combo_box);  

    if (song_name && playlist_name) {
        add_song_to_playlist(song_name, playlist_name);
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "No song or playlist selected.");
    }
}

void on_create_playlist_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create Playlist", GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "Create", GTK_RESPONSE_ACCEPT,
                                                   "Cancel", GTK_RESPONSE_REJECT,
                                                   NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter Playlist Name");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 0);
    
    gtk_widget_show_all(dialog);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_ACCEPT) {
        const char *playlist_name = gtk_entry_get_text(GTK_ENTRY(entry));
        create_playlist(playlist_name);
    }
    
    gtk_widget_destroy(dialog); 
}

void on_play_playlist_button_clicked(GtkWidget *widget, gpointer data) {
    const char *playlist_name = gtk_combo_box_text_get_active_text(playlist_combo_box); 
    if (playlist_name == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "No playlist selected.");
        return;
    }

    char playlist_file[512];
    snprintf(playlist_file, sizeof(playlist_file), "%s/%s.txt", PLAYLISTS_DIR, playlist_name);

    FILE *file = fopen(playlist_file, "r");
    if (!file) {
        gtk_label_set_text(GTK_LABEL(status_label), "Error opening playlist file.");
        return;
    }

    if (!is_empty(&song_list)) {
        Node *temp = song_list.head;
        do {
            Node *next = temp->next;
            free(temp->song_name);
            free(temp);
            temp = next;
        } while (temp != song_list.head);
        init_list(&song_list);
    }

    char song_path[512];
    while (fgets(song_path, sizeof(song_path), file)) {
        song_path[strcspn(song_path, "\n")] = '\0'; 
        if (strlen(song_path) > 0) {
            add_song(&song_list, song_path);
        }
    }
    fclose(file);

    if (!is_empty(&song_list)) {
        current_song = song_list.head;
        play_song(current_song->song_name);
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "Playlist is empty.");
    }
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            if (is_loop_enabled && current_song) {
                play_song(current_song->song_name);
            } else {
                play_next_song();
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
        music_dir = g_strdup(selected_dir);
        save_music_directory(music_dir); 
        load_songs_from_directory();
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

    directory_label = gtk_label_new("Directory: ");
    gtk_box_pack_start(GTK_BOX(vbox), directory_label, FALSE, FALSE, 0);
    update_directory_label();

    change_dir_button = gtk_button_new_with_label("Change Directory");
    g_signal_connect(change_dir_button, "clicked", G_CALLBACK(change_music_directory_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), change_dir_button, FALSE, FALSE, 0);

    url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "Enter the song URL here");
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    GtkWidget *download_button = gtk_button_new_with_label("Download Song");
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), download_button, FALSE, FALSE, 0);

    GtkWidget *create_playlist_button = gtk_button_new_with_label("Create New Playlist");
    g_signal_connect(create_playlist_button, "clicked", G_CALLBACK(on_create_playlist_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), create_playlist_button, FALSE, FALSE, 0);
}

void open_settings_window(GtkWidget *widget, gpointer data) {
    if (!settings_window) {
        settings_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(settings_window), "Settings");
        g_signal_connect(settings_window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
        create_settings_ui();
    }

    gtk_widget_show_all(settings_window);
}

void create_ui() {
    gtk_window_set_default_size(GTK_WINDOW(main_window), 400, 300);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    current_time_label = gtk_label_new("0:00");
    total_time_label = gtk_label_new("0:00");
    gtk_box_pack_start(GTK_BOX(time_box), current_time_label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(time_box), total_time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), time_box, FALSE, FALSE, 0);

    seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);
    g_signal_connect(seek_scale, "value-changed", G_CALLBACK(on_seek_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), seek_scale, FALSE, FALSE, 0);

    GtkWidget *controls_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), controls_hbox, FALSE, FALSE, 0);

    GtkWidget *previous_button = gtk_button_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(previous_button, "clicked", G_CALLBACK(previous_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), previous_button, TRUE, TRUE, 0);

    play_pause_button = gtk_button_new();
    GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(play_pause_button), play_icon);
    g_signal_connect(play_pause_button, "clicked", G_CALLBACK(play_pause_button_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), play_pause_button, TRUE, TRUE, 0);

    GtkWidget *next_button = gtk_button_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(next_button, "clicked", G_CALLBACK(next_song_button), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), next_button, TRUE, TRUE, 0);

    status_label = gtk_label_new("Playing Song...");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

    GtkWidget *extra_controls_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), extra_controls_hbox, FALSE, FALSE, 0);

    GtkWidget *loop_button = gtk_button_new();
    GtkWidget *loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat", GTK_ICON_SIZE_BUTTON); 
    gtk_button_set_image(GTK_BUTTON(loop_button), loop_icon);
    g_signal_connect(loop_button, "clicked", G_CALLBACK(toggle_loop), NULL);
    gtk_box_pack_start(GTK_BOX(extra_controls_hbox), loop_button, TRUE, TRUE, 0);

    GtkWidget *shuffle_button = gtk_button_new();
    GtkWidget *shuffle_icon = gtk_image_new_from_icon_name("media-playlist-shuffle", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(shuffle_button), shuffle_icon);
    g_signal_connect(shuffle_button, "clicked", G_CALLBACK(toggle_shuffle), NULL);
    gtk_box_pack_start(GTK_BOX(extra_controls_hbox), shuffle_button, TRUE, TRUE, 0);

    GtkWidget *volume_label = gtk_label_new("Volume:");
    gtk_box_pack_start(GTK_BOX(vbox), volume_label, FALSE, FALSE, 0);

    volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0); 
    gtk_range_set_value(GTK_RANGE(volume_slider), 100.0);
    g_signal_connect(volume_slider, "value-changed", G_CALLBACK(on_volume_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), volume_slider, FALSE, FALSE, 0);

    playlist_combo_box = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(playlist_combo_box), FALSE, FALSE, 0);

    add_to_playlist_button = gtk_button_new_with_label("Add to Playlist");
    g_signal_connect(add_to_playlist_button, "clicked", G_CALLBACK(on_add_to_playlist_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), add_to_playlist_button, FALSE, FALSE, 0);

    GtkWidget *play_playlist_button = gtk_button_new_with_label("Play Playlist");
    g_signal_connect(play_playlist_button, "clicked", G_CALLBACK(on_play_playlist_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), play_playlist_button, FALSE, FALSE, 5);

    load_playlists();

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

    music_dir = load_music_directory();

    if (!music_dir) {
        ask_for_music_directory();
    } else {
        load_songs_from_directory();
    }

    pipeline = gst_element_factory_make("playbin", "player");

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, NULL);  
    gst_object_unref(bus);

    create_ui();
    add_css_style();

    toggle_shuffle(NULL, NULL);

    if (!is_empty(&song_list)) {
        current_song = song_list.head;
        play_song(current_song->song_name);
    }

    gtk_widget_show_all(main_window);
    gtk_main();

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}
