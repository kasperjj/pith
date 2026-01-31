/*
 * main.c - Pith entry point
 * 
 * This is the main entry point that ties together the runtime and UI.
 * It handles command-line arguments, initializes the system, and runs
 * the main loop.
 */

#include "pith_runtime.h"
#include "pith_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

/* Global debug flag */
bool g_debug = false;

/* ========================================================================
   FILE SYSTEM IMPLEMENTATION
   ======================================================================== */

static char* fs_read_file(const char *path, void *userdata) {
    (void)userdata;
    
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *contents = malloc(size + 1);
    if (!contents) {
        fclose(f);
        return NULL;
    }
    
    fread(contents, 1, size, f);
    contents[size] = '\0';
    fclose(f);
    
    return contents;
}

static bool fs_write_file(const char *path, const char *contents, void *userdata) {
    (void)userdata;
    
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    
    size_t len = strlen(contents);
    size_t written = fwrite(contents, 1, len, f);
    fclose(f);
    
    return written == len;
}

static bool fs_file_exists(const char *path, void *userdata) {
    (void)userdata;
    
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static char** fs_list_dir(const char *path, size_t *count, void *userdata) {
    (void)userdata;
    
    *count = 0;
    
#ifdef _WIN32
    /* Windows implementation */
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(search_path, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    
    /* Count entries first */
    size_t capacity = 16;
    char **entries = malloc(capacity * sizeof(char*));
    
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        
        if (*count >= capacity) {
            capacity *= 2;
            entries = realloc(entries, capacity * sizeof(char*));
        }
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", path, fd.cFileName);
        entries[(*count)++] = strdup(full_path);
        
    } while (FindNextFile(h, &fd));
    
    FindClose(h);
    return entries;
#else
    /* POSIX implementation */
    DIR *dir = opendir(path);
    if (!dir) return NULL;
    
    size_t capacity = 16;
    char **entries = malloc(capacity * sizeof(char*));
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (*count >= capacity) {
            capacity *= 2;
            entries = realloc(entries, capacity * sizeof(char*));
        }
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        entries[(*count)++] = strdup(full_path);
    }
    
    closedir(dir);
    return entries;
#endif
}

/* ========================================================================
   MAIN
   ======================================================================== */

static void print_usage(const char *program) {
    printf("Usage: %s [options] [project_path]\n", program);
    printf("\n");
    printf("Opens a project directory in Pith.\n");
    printf("If no path is given, opens the current directory.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help    Show this help message\n");
    printf("  -v, --version Show version information\n");
    printf("  -d, --debug   Enable debug output (parsing, execution, rendering)\n");
}

static void print_version(void) {
    printf("Pith 0.1.0\n");
    printf("A minimal, stack-based editor runtime.\n");
}

int main(int argc, char *argv[]) {
    /* Parse command line arguments */
    const char *project_path = ".";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_debug = true;
            continue;
        }
        /* First non-flag argument is the project path */
        project_path = argv[i];
    }
    
    /* Set up file system callbacks */
    PithFileSystem fs = {
        .read_file = fs_read_file,
        .write_file = fs_write_file,
        .file_exists = fs_file_exists,
        .list_dir = fs_list_dir,
        .userdata = NULL,
    };
    
    /* Create runtime */
    PithRuntime *rt = pith_runtime_new(fs);
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }
    
    /* Load project */
    if (!pith_runtime_load_project(rt, project_path)) {
        fprintf(stderr, "Failed to load project: %s\n", pith_get_error(rt));
        pith_runtime_free(rt);
        return 1;
    }

    if (g_debug) {
        pith_debug_print_state(rt);
    }

    /* Run init slot if present */
    pith_runtime_run_slot(rt, "init");
    if (rt->has_error) {
        fprintf(stderr, "Error in init: %s\n", pith_get_error(rt));
        pith_runtime_free(rt);
        return 1;
    }

    /* Mount UI if present */
    bool has_ui = pith_runtime_mount_ui(rt);
    PithView *view = has_ui ? pith_runtime_get_view(rt) : NULL;

    if (view) {
        /* Create UI window */
        PithUIConfig ui_config = pith_ui_default_config();
        ui_config.verbose = g_debug;

        /* Build window title */
        char title[256];
        snprintf(title, sizeof(title), "Pith - %s", project_path);
        ui_config.title = title;

        PithUI *ui = pith_ui_new(ui_config);
        if (!ui) {
            fprintf(stderr, "Failed to create UI\n");
            pith_runtime_free(rt);
            return 1;
        }

        /* Main loop */
        while (!pith_ui_should_close(ui)) {
            /* Begin frame */
            pith_ui_begin_frame(ui);

            /* Poll and handle events */
            PithEvent event;
            while ((event = pith_ui_poll_event(ui)).type != EVENT_NONE) {
                /* Handle textfield input if focused */
                if (pith_ui_handle_textfield_input(ui, event)) {
                    continue; /* Input was consumed by textfield */
                }

                /* Handle click events for focus and buttons */
                if (event.type == EVENT_CLICK && view) {
                    PithView *hit = pith_ui_hit_test(ui, view, event.as.click.x, event.as.click.y);
                    PithView *old_focus = pith_ui_get_focus(ui);

                    /* Commit old focus's content before changing focus */
                    if (old_focus && old_focus != hit) {
                        pith_ui_commit_text_widget(old_focus);
                    }

                    if (hit && (hit->type == VIEW_TEXTFIELD || hit->type == VIEW_TEXTAREA)) {
                        pith_ui_set_focus(ui, hit);
                        pith_ui_click_to_cursor(hit, event.as.click.x, event.as.click.y);
                    } else if (hit && hit->type == VIEW_BUTTON) {
                        /* Execute button's on_click block */
                        if (hit->as.button.on_click) {
                            pith_execute_block(rt, hit->as.button.on_click);
                        }
                        pith_ui_set_focus(ui, NULL);
                    } else if (hit && hit->type == VIEW_OUTLINE) {
                        /* Handle outline click - toggle collapse or execute on_click */
                        PithOutlineNode *node = pith_ui_outline_click(hit, event.as.click.y);
                        if (node && node->on_click) {
                            pith_execute_block(rt, node->on_click);
                        }
                        pith_ui_set_focus(ui, NULL);
                    } else {
                        pith_ui_set_focus(ui, NULL);
                    }
                }

                /* Pass event to runtime */
                pith_runtime_handle_event(rt, event);
            }

            /* Check for dirty signals and re-render UI if needed */
            if (pith_runtime_has_dirty_signals(rt)) {
                /* Clear focus before freeing old view (but remember signal for restoration) */
                pith_ui_set_focus(ui, NULL);

                /* Free old view */
                if (rt->current_view) {
                    pith_view_free(rt->current_view);
                    rt->current_view = NULL;
                }
                /* Rebuild view tree */
                pith_runtime_mount_ui(rt);
                /* Restore focus to view with same signal */
                pith_ui_restore_focus(ui, rt->current_view);
                /* Clear dirty flags */
                pith_runtime_clear_dirty(rt);
            }

            /* Get view tree from runtime and render */
            view = pith_runtime_get_view(rt);
            if (g_debug) {
                static bool first_frame = true;
                if (first_frame) {
                    fprintf(stderr, "[DEBUG] View hierarchy:\n");
                    if (view) {
                        pith_debug_print_view(view, 0);
                    } else {
                        fprintf(stderr, "[DEBUG] No view!\n");
                    }
                    first_frame = false;
                }
            }
            if (view) {
                pith_ui_render(ui, view);
            }

            /* End frame */
            pith_ui_end_frame(ui);
        }

        /* Cleanup UI */
        pith_ui_free(ui);
    } else if (g_debug) {
        fprintf(stderr, "[DEBUG] No ui slot, skipping window\n");
    }

    /* Run main slot if present */
    pith_runtime_run_slot(rt, "main");
    if (rt->has_error) {
        fprintf(stderr, "Error in main: %s\n", pith_get_error(rt));
        pith_runtime_free(rt);
        return 1;
    }

    /* Run exit slot if present */
    pith_runtime_run_slot(rt, "exit");
    if (rt->has_error) {
        fprintf(stderr, "Error in exit: %s\n", pith_get_error(rt));
        pith_runtime_free(rt);
        return 1;
    }

    /* Cleanup */
    pith_runtime_free(rt);

    return 0;
}
