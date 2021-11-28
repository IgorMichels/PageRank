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
    float radius;
    int numLinks;
    GList *links;
    float rank;
    int numVisitors[2]; //numVisitors[cycle] = current value
} Site;

GList *sites;
int numSites;
int totalVisitors;
int cycle;

GList *selSite;
gboolean moving;
gboolean reverse;

Pos2D cursorPos;
GtkWidget *window, *canvas;
GtkScale *p_scale, *interval_scale, *radius_scale;

GRand *grand;

#define FPS 120
float g_time;
float g_dt;

float g_updatetime;

float update_interval;

float radius;
float p;

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
    Site *s = sl->data;
    totalVisitors -= s->numVisitors[cycle];
    numSites--;
    g_list_free(s->links);
    g_list_foreach(sites, killLinksInSite, s);
    g_free(s);
    sites = g_list_delete_link(sites, sl);
}

Site *pickSite(Site *src)
{
    if(numSites <= 1) return src;

    Site *s;

    float r = g_rand_double(grand);

    if(r <= p)
    {
        int n = g_rand_int_range(grand, 0, numSites);
        return g_list_nth_data(sites, n);
    }
    else
    {
        if(src->numLinks == 0) return src;
        int n = g_rand_int_range(grand, 0, src->numLinks);
        return g_list_nth_data(src->links, n);
    }

    return NULL;
}

void drawSites(gpointer data, gpointer user_data)
{
    Site *s = data;
    cairo_t *cr = user_data;
    Color col;
    
    //compute rank
    float rank = ((float)s->numVisitors[cycle])/totalVisitors;
    if(totalVisitors == 0) rank = 0;

    //move all visitors
    if(g_time >= g_updatetime)
    while(s->numVisitors[cycle] > 0)
    {
        s->numVisitors[cycle]--;
        Site *n = pickSite(s);
        n->numVisitors[cycle^1]++;
    }

    //smooth rank update
    float diff = rank - s->rank;
    if(fabs(diff) < 0.01)
        s->rank = rank;
    else 
    {
        diff /= fabs(diff);
        s->rank += diff*g_dt/1000;
        s->rank = fabs(s->rank);
        if(s->rank > 1) s->rank = 1;
    }

    s->radius = s->rank*radius + radius;


    //inner circle
    col = (Color){s->rank, 1-s->rank, 0};
    cairo_new_sub_path(cr);
    cairo_arc(cr, s->pos.x, s->pos.y, s->radius, 0, 2*M_PI);
    cairo_set_source_rgb(cr, col.r, col.g, col.b);
    cairo_fill_preserve(cr);

    //outer ring
    col = (Color){0.5, 0.1, 0.8};
    if(selSite) if(s == selSite->data) col = (Color){1, 1, 1};
    cairo_set_line_width(cr, 4);
    cairo_set_source_rgb(cr, col.r, col.g, col.b);
    cairo_stroke(cr);

    //rank text
    char buffer[16];    
    sprintf(buffer, "%1.2f", s->rank);

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

    /*if(selSite)
    {
        Site *s = selSite->data;
        sprintf(buffer, "Selected site rank: %1.2f", s->rank);
        cairo_move_to(cr, 30, 60);
        cairo_show_text(cr, buffer);
    }*/

    g_list_foreach(sites, drawSiteLinks, cr);
    g_list_foreach(sites, drawSites, cr);

    if(g_time > g_updatetime)
    {
        g_updatetime = g_time + update_interval;
        cycle = cycle ^ 1;
    }
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
        case 's': //create site
            {
                Site *s = (Site*)g_malloc(sizeof(Site));
                s->pos = cursorPos;
                s->radius = radius;
                s->numLinks = 0;
                s->links = NULL;
                s->numVisitors[0] = 0;
                s->numVisitors[1] = 0;
                s->rank = 0;
                sites = g_list_prepend(sites, s);
                numSites++;
            }
            break;
        case 'k': //kill selected site
            {
                if(!selSite) break;
                killSite(selSite);
                selSite = NULL;
            }
            break;
        case 'K':
            {
                selSite = NULL;
                GList *cur = sites;
                while(cur != NULL)
                {
                    GList *next = cur->next;
                    killSite(cur);
                    cur = next;
                }
            }
            break;
        case 'l':
        case 'L': //add/remove = l/L link
            {
                GList *sel = selectSite();
                if(!sel) break;
                if(!selSite) break;
                Site *dst = sel->data;
                Site *s = (Site*)selSite->data;

                if(reverse)
                {
                    Site *tmp = s;
                    s = dst;
                    dst = tmp;
                }

                killLinksInSite(s, dst);
                if(event->keyval == 'L') break;
    
                s->links = g_list_prepend(s->links, dst);
                s->numLinks++;
            }
            break;
        case 'v':
        case 'V':
            {
                int num = 1;
                if(event->keyval == 'V') num = 1000;
                if(!selSite) break;
                Site* s = selSite->data;
                s->numVisitors[cycle] += num;
                totalVisitors += num;
            }
            break;
        case 'e': //clear visitors
            {
                GList *cur = sites;
                while(cur != NULL)
                {
                    GList *next = cur->next;
                    Site *s = cur->data;
                    s->numVisitors[0] = 0;
                    s->numVisitors[1] = 0;
                    cur = next;
                }
                totalVisitors = 0;
            }
            break;
        case 'm': moving = !moving; break; //move selected site
        case 'r': reverse = !reverse; break; //link direction
    }
    return TRUE;
}

void scaleChange(GtkRange *range, gpointer user_data)
{
    if(range == GTK_RANGE(p_scale)) p = gtk_range_get_value(range);
    if(range == GTK_RANGE(interval_scale)) update_interval = gtk_range_get_value(range);
    if(range == GTK_RANGE(radius_scale)) radius = gtk_range_get_value(range);
}

int main(int argc, char *argv[])
{
    sites = NULL;
    numSites = 0;
    totalVisitors = 0;

    selSite = NULL;
    moving = FALSE;
    reverse = FALSE;

    update_interval = 1;
    p = 0.1;
    radius = 20;
    cycle = 0;

    g_time = 0;
    g_dt = 1000.0/FPS;
    g_updatetime = update_interval;

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

    p_scale = GTK_SCALE(gtk_builder_get_object(builder, "p_scale"));
    interval_scale = GTK_SCALE(gtk_builder_get_object(builder, "interval_scale"));
    radius_scale = GTK_SCALE(gtk_builder_get_object(builder, "radius_scale"));

    gtk_builder_connect_signals(builder, NULL);

    g_timeout_add(g_dt, (GSourceFunc)timer, canvas);

    gtk_widget_show_all(window);
    gtk_main();

    gtk_widget_destroy(window);
    return EXIT_SUCCESS;
}