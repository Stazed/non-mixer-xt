
/*******************************************************************************/
/* Copyright (C) 2013 Jonathan Moore Liles                                     */
/*                                                                             */
/* This program is free software; you can redistribute it and/or modify it     */
/* under the terms of the GNU General Public License as published by the       */
/* Free Software Foundation; either version 2 of the License, or (at your      */
/* option) any later version.                                                  */
/*                                                                             */
/* This program is distributed in the hope that it will be useful, but WITHOUT */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   */
/* more details.                                                               */
/*                                                                             */
/* You should have received a copy of the GNU General Public License along     */
/* with This program; see the file COPYING.  If not,write to the Free Software */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */
/*******************************************************************************/

#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Input.H>

#include "../../FL/Fl_Scalepack.H"
#include "../../FL/Plugin_Chooser_UI.H"

#include "Plugin_Module.H"
#include "Plugin_Chooser.H"
#include "stdio.h"
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>

#include <algorithm>

static    std::vector <Plugin_Module::Plugin_Info*> _plugin_rows;
static int previous_favorites = 1;
static int plugin_type = 0;

Module::Picked
Plugin_Chooser::plugin_chooser ( int ninputs )
{
    Module::Picked picked = { false, 0 };

    Plugin_Chooser *o = new Plugin_Chooser( 0,0,735,500,"Plugin Chooser");

    o->ui->favorites_button->value(previous_favorites);
    o->ui->all_button->value(previous_favorites ? 0 : 1);
    o->ui->type_choice->value(plugin_type);
    o->ui->inputs_input->value( ninputs );

    o->search( "", "", "Any", ninputs, 0, o->ui->favorites_button->value(), o->ui->type_choice->text() );

    o->show();
    
    while ( o->shown() )
        Fl::wait();

    if (const char* const uri = o->uri())
    {
        picked.is_lv2 = true;
        picked.uri = uri;
    }
    else
    {
        picked.is_lv2 = false;
        picked.unique_id = o->value();
    }

    previous_favorites = o->ui->favorites_button->value();
    plugin_type = o->ui->type_choice->value();

    delete o;

    return picked;
}

void
Plugin_Chooser::search ( const char *name, const char *author, const char *category,
                        int ninputs, int noutputs, bool favorites, const char *plug_type )
{
    _plugin_rows.clear();
    
    for ( std::list<Plugin_Module::Plugin_Info>::iterator i = _plugins.begin(); i != _plugins.end(); ++i )
    {
        Plugin_Module::Plugin_Info *p = &(*i);

        if ( strcasestr( p->name.c_str(), name ) &&
             strcasestr( p->author.c_str(), author ) )
        {
#if 0
            /* MIDI only. Looks like these work now! */
            if ( p->audio_outputs == 0 )
                continue;
#endif
            /* MAX_PORTS is an arbitrary limit, could be more if we really needed it */
            if ( p->audio_outputs > MAX_PORTS)
                continue;

            if ( !
                 ((( ( ninputs == 0 || ninputs == p->audio_inputs || ( ninputs == 1 && p->audio_inputs == 2 ) ) ) &&
                  ( noutputs == 0 || noutputs == p->audio_outputs )) ||
                  ( p->audio_inputs == 1 && p->audio_outputs == 1 ) ||
                  (p->audio_inputs == 0 && ninputs == 1) ) )    // this would be a synth with no inputs
                continue;

            if ( favorites > 0 && ! p->favorite )
                continue;

            if ( strcmp( category, "Any" ) )
            {
                if ( !p->category.c_str() && strcmp( category, "Unclassified" ))
                    continue;

                if (strncmp( p->category.c_str(), category, strlen( category )))
                    continue;
            }

            // if we want LV2 types only
            if( !strcmp( plug_type, "LV2" ) )
            {
                if( strcmp(p->type, "LV2") )
                {
                    continue;   // Not LV2 so skip it
                }
            }
            else if( !strcmp( plug_type, "LADSPA" ) )   // if we want LADSPA only
            {
                if( strcmp( p->type, "LADSPA" ) )
                {
                    continue;   // Not LADSPA so skip it
                }
            }

            _plugin_rows.push_back( p );
        }
    }

    ui->table->rows( _plugin_rows.size() );
}

void
Plugin_Chooser::cb_handle ( Fl_Widget *w, void *v )
{
    ((Plugin_Chooser*)v)->cb_handle( w );
}

void
Plugin_Chooser::cb_handle ( Fl_Widget *w )
{
    if ( w == ui->all_button )
    {
        ui->favorites_button->value( !ui->all_button->value() );
    }

    {  
        char picked[512];
        ui->category_choice->item_pathname( picked, sizeof( picked ) );
  
        search( ui->name_input->value(), 
                ui->author_input->value(),
                picked[0] == '/' ? &picked[1] : picked,
                ui->inputs_input->value(), 
                ui->outputs_input->value(),
                ui->favorites_button->value(),
                ui->type_choice->text());
    }
}

class Plugin_Table : public Fl_Table_Row
{
protected:
    void draw_cell(TableContext context,  		// table cell drawing
    		   int R=0, int C=0, int X=0, int Y=0, int W=0, int H=0);
public:
    Plugin_Table(int x, int y, int w, int h, const char *l=0) : Fl_Table_Row(x,y,w,h,l)
    {
	end();
    }
    ~Plugin_Table() { }
};

void Plugin_Table::draw_cell(TableContext context, 
			  int R, int C, int X, int Y, int W, int H)
{
    const char *headings[] = { "Fav.", "Name", "Author", "Type", "In", "Out" };
 
    static char s[40];

    switch ( context )
    {
	case CONTEXT_STARTPAGE:
	    fl_font(FL_HELVETICA, 12);
	    return;

	case CONTEXT_COL_HEADER:
	    fl_push_clip(X, Y, W, H);
	    {
	        fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, col_header_color());
	        fl_color(FL_FOREGROUND_COLOR);
		fl_draw(headings[C], X, Y, W, H, FL_ALIGN_CENTER);
	    }
	    fl_pop_clip();
	    return;

	case CONTEXT_ROW_HEADER:
	    return;

	case CONTEXT_CELL:
	{
            fl_font(FL_HELVETICA, 12);
	  
            const char *s2 = (char*)s;
            Fl_Align a = FL_ALIGN_CENTER;
            int symbol = 0;
            Fl_Color c = FL_FOREGROUND_COLOR;
            switch ( C ) 
            {
                case 0:
                    sprintf( s, "%s", "@circle" );
                    c = _plugin_rows[R]->favorite ? FL_LIGHT2 : FL_BLACK;
                    symbol = 1;
                    fl_font(FL_HELVETICA, 9 );
                    break;
                case 1:
                    a = FL_ALIGN_LEFT;
                    s2 = _plugin_rows[R]->name.c_str();
                    break;
                case 2:
                    a = FL_ALIGN_LEFT;
                    s2 = _plugin_rows[R]->author.c_str();
                    break;
                case 3:
                    s2 = _plugin_rows[R]->type;
                    break;
                case 4:
                    sprintf( s, "%i", _plugin_rows[R]->audio_inputs );
                    break;
                case 5:
                    sprintf( s, "%i", _plugin_rows[R]->audio_outputs );
                    break;
            }

            fl_color( row_selected(R) ? selection_color() : FL_DARK1);
            fl_rectf(X, Y, W, H);
            fl_color(color()); 
            fl_rect(X, Y, W, H);

            X+=4;
            W-=8;
            Y+=4;
            H-=8;

	    fl_push_clip(X, Y, W, H);
	    
            fl_color(c);
            fl_draw(s2, X, Y, W, H, a, 0, symbol );

            fl_pop_clip();
	    return;
	}

	case CONTEXT_TABLE:
	    fprintf(stderr, "TABLE CONTEXT CALLED\n");
	    return;

	case CONTEXT_ENDPAGE:
	case CONTEXT_RC_RESIZE:
	case CONTEXT_NONE:
	    return;
    }
}

void
Plugin_Chooser::cb_table ( Fl_Widget *w, void *v )
{
    ((Plugin_Chooser*)v)->cb_table(w);
}

void
Plugin_Chooser::cb_table ( Fl_Widget *w )
{
    Fl_Table_Row *o = (Fl_Table_Row*)w;

    int R = o->callback_row();
    int C = o->callback_col();

    Fl_Table::TableContext context = o->callback_context();

    if ( context == Fl_Table::CONTEXT_CELL )
    {
        if ( C == 0 )
        {
            _plugin_rows[R]->favorite = ! _plugin_rows[R]->favorite;
            o->redraw();
        }
        else
        {
            if (::strcmp(_plugin_rows[R]->type, "LV2") == 0)
                _uri   = _plugin_rows[R]->path;
            else
                _value = _plugin_rows[R]->id;
            hide();
        }
    }
}

extern char *user_config_dir;

static FILE *open_favorites( const char *mode )
{
    char *path;

    asprintf( &path, "%s/%s", user_config_dir, "favorite_plugins" );

    FILE *fp = fopen( path, mode );
        
    free( path );

    return fp;
}

int
Plugin_Chooser::load_favorites ( void )
{
    FILE *fp = open_favorites( "r" );
    
    if ( !fp )
    {
        return 0;
    }

    unsigned long id;
    char *type;
    char *path;
    int favorites = 0;

    while ( 3 == fscanf( fp, "%m[^:]:%lu:%m[^]\n]\n", &type, &id, &path ) )
    {
        for ( std::list<Plugin_Module::Plugin_Info>::iterator i = _plugins.begin();
              i != _plugins.end();
              ++i )
        {
            if ( !strcmp( (*i).type, type ) &&
                 (*i).id == id )
            {
                if( !strcmp(type, "LV2") )
                {
                    if( !strcmp(path, (*i).path) )
                    {
                        (*i).favorite = 1;
                        favorites++;
                    }
                }
                else
                {
                    (*i).favorite = 1;
                    favorites++;
                }
            }
        }

        free(path);
        free(type);
    }

    fclose(fp);

    return favorites;
}

void
Plugin_Chooser::save_favorites ( void )
{
    FILE *fp = open_favorites( "w" );
    
    if ( !fp )
        return;
    
    for ( std::list<Plugin_Module::Plugin_Info>::iterator i = _plugins.begin();
          i != _plugins.end();
          ++i )
    {
        if ( (*i).favorite )
        {
            fprintf( fp, "%s:%lu:%s\n", i->type, i->id, i->path );
        }
    }
    
    fclose( fp );
}

void 
Plugin_Chooser::load_categories ( void )
{
    ui->category_choice->add( "Any" );

    std::list<std::string> categories;

    for ( std::list<Plugin_Module::Plugin_Info>::iterator i = _plugins.begin();
          i != _plugins.end();
          ++i )
    {
        if ( i->category.c_str() )
        {
            categories.push_back(i->category);
        }
    }
    
    categories.sort();


    for ( std::list<std::string>::const_iterator i = categories.begin();
          i != categories.end();
          ++i )
    {
        ui->category_choice->add( i->c_str() );
    }

    ui->category_choice->value( 0 );
}

Plugin_Chooser::Plugin_Chooser ( int X,int Y,int W,int H, const char *L )
    : Fl_Double_Window ( X,Y,W,H,L )
{
    set_modal();
    _uri = NULL;
    _value = 0;
   
    _plugins = Plugin_Module::get_all_plugins();


    {
        Plugin_Chooser_UI *o = ui = new Plugin_Chooser_UI(X,Y,W,H);

        o->name_input->callback( &Plugin_Chooser::cb_handle, this );
        o->name_input->when( FL_WHEN_CHANGED );


        o->author_input->callback( &Plugin_Chooser::cb_handle, this );
        o->author_input->when( FL_WHEN_CHANGED );


        o->inputs_input->callback( &Plugin_Chooser::cb_handle, this );
        o->inputs_input->when( FL_WHEN_CHANGED );

        o->outputs_input->callback( &Plugin_Chooser::cb_handle, this );
        o->outputs_input->when( FL_WHEN_CHANGED );

        o->favorites_button->callback( &Plugin_Chooser::cb_handle, this );
        o->favorites_button->when( FL_WHEN_CHANGED );

        o->all_button->callback( &Plugin_Chooser::cb_handle, this );
        o->all_button->when( FL_WHEN_CHANGED );


        o->category_choice->callback( &Plugin_Chooser::cb_handle, this );
        o->category_choice->when( FL_WHEN_CHANGED );
        
        o->type_choice->callback(&Plugin_Chooser::cb_handle, this );
        o->type_choice->when( FL_WHEN_CHANGED );

        {
            Plugin_Table *o = new Plugin_Table(ui->table->x(),ui->table->y(),ui->table->w(),ui->table->h() );
            ui->table_group->add(o);
            ui->table_group->resizable(o);
            delete ui->table;
            ui->table = o;
            /* ui->scalepack->add( o ); */
            /* ui->scalepack->resizable( o ); */
            o->col_header(1);
            o->col_resize(1);
            o->row_resize(1);
            o->cols(6);
            o->col_resize_min(4);
            o->col_width(0,30);
            o->col_width(1,350 - 7);
            o->col_width(2,200);
            o->col_width(3,75);
            o->col_width(4,30);
            o->col_width(5,30);
            o->color(FL_BLACK);
            o->box(FL_NO_BOX);
            o->when(FL_WHEN_CHANGED);
            o->callback( &Plugin_Chooser::cb_table, this );
            
        }

        resizable(o);
    }
    size_range( 735, 300, 735, 0 );
    
    end();

    load_categories();

    if ( load_favorites() )
    {
        ui->all_button->value(0);
        ui->favorites_button->value(1);
    }
}

Plugin_Chooser::~Plugin_Chooser( )
{
    save_favorites();
}
