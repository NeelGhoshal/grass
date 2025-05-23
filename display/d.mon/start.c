#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <grass/gis.h>
#include <grass/spawn.h>
#include <grass/display.h>
#include <grass/glocale.h>

#include "proto.h"

static char *start(const char *, const char *, int, int, int);
static char *start_wx(const char *, const char *, int, int, int);
static void error_handler(void *);

/* start file-based monitor */
char *start(const char *name, const char *output, int width, int height,
            int update)
{
    char *output_path;
    const char *output_name;

    /* stop monitor on failure */
    G_add_error_handler(error_handler, (char *)name);

    /* full path for output file */
    output_path = (char *)G_malloc(GPATH_MAX);
    output_path[0] = '\0';

    if (!output) {
        char buff[512];

        snprintf(buff, sizeof(buff), "GRASS_RENDER_IMMEDIATE=%s", name);
        putenv(G_store(buff));
        snprintf(buff, sizeof(buff), "GRASS_RENDER_WIDTH=%d", width);
        putenv(G_store(buff));
        snprintf(buff, sizeof(buff), "GRASS_RENDER_HEIGHT=%d", height);
        putenv(G_store(buff));

        D_open_driver();

        output_name = D_get_file();
        if (!output_name) {
            G_free(output_path);
            return NULL;
        }
        if (!update && access(output_name, F_OK) == 0) {
            if (G_get_overwrite()) {
                G_warning(_("File <%s> already exists and will be overwritten"),
                          output_name);
                D_setup_unity(0);
                D_erase("white");
            }
            else
                G_fatal_error(_("option <%s>: <%s> exists. To overwrite, use "
                                "the --overwrite flag"),
                              "output", output_name);
        }
        D_close_driver(); /* must be called after check because this
                           * function produces default map file */
        putenv("GRASS_RENDER_IMMEDIATE=");
    }
    else {
        char *dir_name;

        output_name = output;
        /* check write permission */
        dir_name = G_store(output_name);
        if (access(dirname(dir_name), W_OK) != 0)
            G_fatal_error(_("Unable to start monitor, don't have "
                            "write permission for <%s>"),
                          output_name);
        G_free(dir_name);

        /* check if file exists */
        if (!update && access(output_name, F_OK) == 0) {
            if (G_get_overwrite()) {
                G_warning(_("File <%s> already exists and will be overwritten"),
                          output_name);
                if (0 != unlink(output_name))
                    G_fatal_error(_("Unable to delete <%s>"), output_name);
            }
        }
    }

    if (!strchr(output_name, HOST_DIRSEP)) { /* relative path */
        char *ptr;

        if (!getcwd(output_path, GPATH_MAX))
            G_fatal_error(_("Unable to get current working directory"));
        ptr = output_path + strlen(output_path) - 1;
        if (*(ptr++) != HOST_DIRSEP) {
            *(ptr++) = HOST_DIRSEP;
            *(ptr) = '\0';
        }
        strcat(output_path, output_name);
        G_message(_("Output file: %s"), output_path);
    }
    else {
        strcpy(output_path, output_name); /* already full path */
    }

    return output_path;
}

/* start wxGUI display monitor */
char *start_wx(const char *name, const char *element, int width, int height,
               int x_only)
{
    char progname[GPATH_MAX], mon_path[GPATH_MAX];
    char str_width[1024], str_height[1024], *str_x_only;
    char *mapfile;

    /* full path */
    mapfile = (char *)G_malloc(GPATH_MAX);
    mapfile[0] = '\0';

    snprintf(progname, sizeof(progname), "%s/gui/wxpython/mapdisp/main.py",
             G_gisbase());
    snprintf(str_width, sizeof(str_width), "%d", width);
    snprintf(str_height, sizeof(str_height), "%d", height);

    if (x_only)
        str_x_only = "1";
    else
        str_x_only = "0";

    G_file_name(mon_path, element, NULL, G_mapset());
    G_spawn_ex(getenv("GRASS_PYTHON"), progname, progname, name, mon_path,
               str_width, str_height, str_x_only, SF_BACKGROUND, NULL);

    G_file_name(mapfile, element, "map.ppm", G_mapset());

    return mapfile;
}

int start_mon(const char *name, const char *output, int select, int width,
              int height, const char *bgcolor, int truecolor, int x_only,
              int update)
{
    char *mon_path;
    char *out_file, *env_file, *cmd_file, *leg_file;
    char buf[1024];
    char file_path[GPATH_MAX], render_cmd_path[GPATH_MAX];
    int fd;

    if (check_mon(name)) {
        const char *curr_mon;

        curr_mon = G_getenv_nofatal("MONITOR");
        if (select && (!curr_mon || strcmp(curr_mon, name) != 0))
            G_setenv("MONITOR", name);

        G_fatal_error(_("Monitor <%s> already running"), name);
    }

    G_verbose_message(_("Starting monitor <%s>..."), name);

    /* create .tmp/HOSTNAME/u_name directory */
    mon_path = get_path(name, FALSE);
    G_make_mapset_element(mon_path);

    G_file_name(file_path, mon_path, "env", G_mapset());
    env_file = G_store(file_path);
    G_file_name(file_path, mon_path, "cmd", G_mapset());
    cmd_file = G_store(file_path);
    G_file_name(file_path, mon_path, "leg", G_mapset());
    leg_file = G_store(file_path);

    /* create py file (renderer) */
    snprintf(render_cmd_path, sizeof(render_cmd_path),
             "%s/etc/d.mon/render_cmd.py", getenv("GISBASE"));
    G_file_name(file_path, mon_path, "render.py", G_mapset());
    G_debug(1, "Monitor name=%s, pyfile = %s", name, file_path);
    if (1 != G_copy_file(render_cmd_path, file_path))
        G_fatal_error(_("Unable to copy render command file"));

    /* start monitor */
    if (strncmp(name, "wx", 2) == 0)
        out_file = start_wx(name, mon_path, width, height, x_only);
    else
        out_file = start(name, output, width, height, update);

    /* create env file (environmental variables used for rendering) */
    G_debug(1, "Monitor name=%s, envfile=%s", name, env_file);
    fd = creat(env_file, 0666);
    if (fd < 0)
        G_fatal_error(_("Unable to create file <%s>"), env_file);

    if (G_strncasecmp(name, "wx", 2) == 0) {
        snprintf(buf, sizeof(buf),
                 "GRASS_RENDER_IMMEDIATE=default\n"); /* TODO: read settings
                                           from wxGUI */
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
        snprintf(buf, sizeof(buf), "GRASS_RENDER_FILE_READ=FALSE\n");
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
        snprintf(buf, sizeof(buf), "GRASS_RENDER_TRANSPARENT=TRUE\n");
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
    }
    else {
        snprintf(buf, sizeof(buf), "GRASS_RENDER_IMMEDIATE=%s\n", name);
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
        snprintf(buf, sizeof(buf), "GRASS_RENDER_FILE_READ=TRUE\n");
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
    }
    snprintf(buf, sizeof(buf), "GRASS_RENDER_FILE=%s\n", out_file);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
        G_fatal_error(_("Failed to write to file <%s>"), env_file);
    snprintf(buf, sizeof(buf), "GRASS_RENDER_WIDTH=%d\n", width);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
        G_fatal_error(_("Failed to write to file <%s>"), env_file);
    snprintf(buf, sizeof(buf), "GRASS_RENDER_HEIGHT=%d\n", height);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
        G_fatal_error(_("Failed to write to file <%s>"), env_file);
    snprintf(buf, sizeof(buf), "GRASS_LEGEND_FILE=%s\n", leg_file);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
        G_fatal_error(_("Failed to write to file <%s>"), env_file);

    if (bgcolor) {
        if (strcmp(bgcolor, "none") == 0)
            snprintf(buf, sizeof(buf), "GRASS_RENDER_TRANSPARENT=TRUE\n");
        else
            snprintf(buf, sizeof(buf), "GRASS_RENDER_BACKGROUNDCOLOR=%s\n",
                     bgcolor);
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
    }
    if (truecolor) {
        snprintf(buf, sizeof(buf), "GRASS_RENDER_TRUECOLOR=TRUE\n");
        if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
            G_fatal_error(_("Failed to write to file <%s>"), env_file);
    }
    close(fd);

    /* create cmd file (list of GRASS display commands to render) */
    G_debug(1, "Monitor name=%s, cmdfile = %s", name, cmd_file);
    int fd2 = creat(cmd_file, 0666);
    if (0 > fd2)
        G_fatal_error(_("Unable to create file <%s>"), cmd_file);
    close(fd2);

    /* select monitor if requested */
    if (select)
        G_setenv("MONITOR", name);

    G_free(mon_path);
    G_free(out_file);
    G_free(env_file);
    G_free(cmd_file);
    G_free(leg_file);

    return 0;
}

void error_handler(void *p)
{
    const char *name = (const char *)p;

    stop_mon(name);
}
