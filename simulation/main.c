#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>

typedef struct
{
    float x, y;
} Pos2D;

typedef struct
{
    float r, g, b;
} Color;

typedef struct
{
    Pos2D pos;
    Color color;
    float radius;

    int numLinks;
    GList *links;

    float timeDec;
    int numVisitors;
} Site;

GList *sites;
int numSites = 0;
int totalVisitors = 0;

GList *selSite = NULL;
char moving = 0;

Pos2D cursorPos;
GtkWidget *window, *canvas;

GRand *grand;

#define FPS 120
float g_time;
float g_dt;

float radius = 20;
float lambda = 1;
float p = 0.1;

void killLinksInSite(gpointer data, gpointer user_data)
{
    Site *dst = (Site*)user_data;
    Site *src = (Site*)data;

    GList *cur = src->links;
    while(cur != NULL)
    {
        GList *next = cur->next;
        if(cur->data == dst)
        {
            src->links = g_list_delete_link(src->links, cur);
            src->numLinks--;
        }
        cur = next;
    }
}

void killSite(GList *sl)
{
    if(sl == selSite) selSite = NULL;
    Site *s = sl->data;
    totalVisitors -= s->numVisitors;
    numSites--;
    g_list_free(s->links);
    g_list_foreach(sites, killLinksInSite, s);
    g_free(s);
    sites = g_list_delete_link(sites, sl);
    g_print("A site has died\n");
}

Site *pickSite(Site *src)
{
    if(numSites <= 1) return src;

    Site *s = src;

    while(s == src)
    {
        float r = g_rand_double(grand);

        if(!src->numLinks || r <= p)
        {
            int n = g_rand_int_range(grand, 0, numSites);
            s = g_list_nth_data(sites, n);
        }
        else
        {
            int n = g_rand_int_range(grand, 0, src->numLinks);
            s = g_list_nth_data(src->links, n);
        }
    }

    return s;
}

float rand_exp(float lambda)
{
    return -log(g_rand_double(grand))/lambda;
}

void updateTime(Site *s)
{
    if(s->numVisitors > 0)
        s->timeDec = g_time + rand_exp(lambda*s->numVisitors);
}

void drawSites(gpointer data, gpointer user_data)
{
    Site *s = data;
    cairo_t *cr = user_data;
    Color col = s->color;

    float rank = ((float)s->numVisitors)/totalVisitors;
    if(totalVisitors == 0) rank = 0;
    s->radius = radius + radius*rank;
    
    if(selSite)
    if(s == selSite->data)
    {
        col = (Color){0, 0, 1};
    }

    if(s->numVisitors > 0)
    {
        if(g_time > s->timeDec)
        {
            s->numVisitors--;
            updateTime(s);
            Site *n = pickSite(s);
            n->numVisitors++;
            updateTime(n);
        }
    }

    cairo_new_sub_path(cr);
    cairo_arc(cr, s->pos.x, s->pos.y, s->radius, 0, 2*M_PI);
    cairo_set_source_rgb(cr, col.r, col.g, col.b);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 4);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_stroke(cr);

    char buffer[16];    
    sprintf(buffer, "%1.2f", rank);

    cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, s->radius*0.75);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, buffer, &extents);
    cairo_move_to(cr, s->pos.x-extents.width/2.0, s->pos.y+extents.height/2.0);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_show_text(cr, buffer);
}

typedef struct
{
    Site *src;
    cairo_t *cr;
} drawLinkData;

void drawLink(gpointer data, gpointer user_data)
{
    Site *dst = (Site*)data;
    Site *src = ((drawLinkData*)user_data)->src;
    cairo_t *cr = ((drawLinkData*)user_data)->cr;

    Pos2D ds = (Pos2D){dst->pos.x - src->pos.x, dst->pos.y - src->pos.y};
    float len = sqrt(ds.x*ds.x + ds.y*ds.y);
    Pos2D n = (Pos2D){ds.x/len, ds.y/len};
    Pos2D n2 = (Pos2D){-n.y, n.x};

    Pos2D pt_src = (Pos2D){src->pos.x + src->radius*n.x, src->pos.y + src->radius*n.y};
    Pos2D pt_dst = (Pos2D){dst->pos.x - dst->radius*n.x, dst->pos.y - dst->radius*n.y};

    cairo_set_line_width(cr, 2);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, pt_src.x, pt_src.y);
    cairo_line_to(cr, pt_dst.x, pt_dst.y);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);

    float s = 10;

    cairo_move_to(cr, pt_dst.x, pt_dst.y);
    cairo_line_to(cr, pt_dst.x + s*(n2.x-n.x), pt_dst.y + s*(n2.y-n.y));
    cairo_line_to(cr, pt_dst.x + s*(-n2.x-n.x), pt_dst.y + s*(-n2.y-n.y));
    
    cairo_fill(cr);
}

void drawSiteLinks(gpointer data, gpointer user_data)
{
    Site *s = data;
    cairo_t *cr = user_data;

    drawLinkData ld = (drawLinkData){s, cr};
    g_list_foreach(s->links, drawLink, &ld);
}

GList *selectSite()
{
    GList *cur = sites;
    while(cur != NULL)
    {
        GList *next = cur->next;

        Site *s = (Site*)cur->data;
        Pos2D ds = (Pos2D){s->pos.x - cursorPos.x, s->pos.y - cursorPos.y};
        float qs = ds.x*ds.x + ds.y*ds.y;
        if(qs < s->radius*s->radius) return cur;

        cur = next;
    }
    return NULL;
}

gboolean timer(GtkWidget *window)
{
    g_time += g_dt/1000;
    gtk_widget_queue_draw(canvas);
    return TRUE;
}

gboolean windowDelete(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit();
    return FALSE;
}

gboolean canvasDraw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    cairo_set_source_rgb(cr, 0.6, 0.6, 1);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 24);
    cairo_set_source_rgb(cr, 0, 0, 0);

    char buffer[512];
    sprintf(buffer, "Total visitors: %d", totalVisitors);
    cairo_move_to(cr, 30, 30);
    cairo_show_text(cr, buffer);

    if(selSite)
    {
        Site *s = selSite->data;
        sprintf(buffer, "Selected visitors: %d", s->numVisitors);
        cairo_move_to(cr, 30, 60);
        cairo_show_text(cr, buffer);
    }

    g_list_foreach(sites, drawSiteLinks, cr);
    g_list_foreach(sites, drawSites, cr);
    return TRUE;
}

gboolean buttonPress(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    if(event->button == 1) selSite = selectSite();
    return TRUE;
}

gboolean motionNotify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{   
    cursorPos = (Pos2D){event->x, event->y};
    if(moving && selSite)
    {
        Site* s = selSite->data;
        s->pos = cursorPos;
    }
    return TRUE;
}

gboolean keyPress(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    switch(event->keyval)
    {
        case 's':
            {
                Site *s = (Site*)g_malloc(sizeof(Site));
                s->pos = cursorPos;
                s->color = (Color){1, 1, 1};
                s->radius = radius;
                s->numLinks = 0;
                s->links = NULL;
                s->numVisitors = 0;
                sites = g_list_prepend(sites, s);
                numSites++;
            }
            break;
        case 'r':
            {
                GList *dst = selectSite();
                if(!dst) break;
                if(!selSite) break;
                killLinksInSite(selSite->data, dst->data);
            }
            break;
        case 'k':
            {
                if(!selSite) break;
                killSite(selSite);
            }
            break;
        case 'l':
            {
                GList *dst = selectSite();
                if(!dst) break;
                if(!selSite) break;
                killLinksInSite(selSite->data, dst->data);
                Site *s = (Site*)selSite->data;
                s->links = g_list_prepend(s->links, dst->data);
                s->numLinks++;
            }
            break;
        case 'v':
            {
                if(!selSite) break;
                Site* s = selSite->data;
                s->numVisitors++;
                totalVisitors++;
                updateTime(s);
            }
            break;
        case 'e':
            {
                GList *cur = sites;
                while(cur != NULL)
                {
                    GList *next = cur->next;
                    Site *s = cur->data;
                    s->numVisitors = 0;
                    cur = next;
                }
                totalVisitors = 0;
            }
            break;
        case 'm':
            moving = !moving;
            break;
    }
    return TRUE;
}

int main(int argc, char *argv[])
{
    sites = NULL;

    g_time = 0;
    g_dt = 1000.0/FPS;

    grand = g_rand_new();

    gtk_init(&argc, &argv);

    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    if(gtk_builder_add_from_file(builder, "gui.glade", &error) == 0)
    {
        g_print("%s\n", error->message);
        return EXIT_FAILURE;
    }

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
    canvas = GTK_WIDGET(gtk_builder_get_object(builder, "canvas"));

    gtk_builder_connect_signals(builder, NULL);

    g_timeout_add(g_dt, (GSourceFunc)timer, canvas);

    gtk_widget_show_all(window);
    gtk_main();

    gtk_widget_destroy(window);
    return EXIT_SUCCESS;
}