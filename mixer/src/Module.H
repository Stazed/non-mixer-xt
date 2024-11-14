
/*******************************************************************************/
/* Copyright (C) 2008-2021 Jonathan Moore Liles (as "Non-Mixer")               */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
/*                                                                             */
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

#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Group.H>

#include <stdlib.h>

#include "../../nonlib/debug.h"
#include "../../nonlib/Thread.H"
#include "../../nonlib/Loggable.H"
#include "../../nonlib/JACK/Port.H"
#include "../../nonlib/OSC/Endpoint.H"

#include <vector>

#include "lv2/ImplementationData.H"
#include <list>
#include <algorithm>


#ifdef LV2_SUPPORT
#include "lv2/lv2_evbuf.h"
#include "lv2/lilvmm.hpp"
#include <suil/suil.h>
#endif

class Chain;
class Module_Parameter_Editor;
class Fl_Menu_;
class Fl_Menu_Button;
class Fl_Button;
class Mixer_Strip;

enum Plugin_Index
{
    Type_NONE = 0,
    Type_LADSPA,
    Type_LV2,
    Type_CLAP,
    Type_VST2,
    Type_VST3
    // VST2
};

const uint32_t C_MAX_UINT32 = 4294967295;

class Module : public Fl_Group, public Loggable
{

    int _instances;
    Chain *_chain;
    bool _is_default;
    bool _is_jack_module;
    bool _is_zero_synth;
    bool _has_name_change;
    char *_base_label;
    nframes_t _nframes;

    static nframes_t _buffer_size;
    static nframes_t _sample_rate;
    static Module *_copied_module_empty;
    static char *_copied_module_settings;

    int _number;

    virtual void init ( void );

    void insert_menu_cb ( const Fl_Menu_ *m );
    static void insert_menu_cb ( Fl_Widget *w, void *v );

    void menu_cb ( const Fl_Menu_ *m );
    static void menu_cb ( Fl_Widget *w, void *v );
    Fl_Menu_Button & menu ( void ) const;

    bool copy ( void ) const;
    void paste_before ( void );

protected:

    float * _bypass;

public:

    Module_Parameter_Editor *_editor;
    unsigned int _plug_type;    // LADSPA, LV2, etc
    bool _is_from_custom_ui;
    bool _is_removed;
    bool _use_custom_data;

    void deleteEditor();
    virtual int number ( void ) const
    {
        return _number;
    }
    virtual void number ( int v );

    virtual const char * base_label ( void ) const
    {
        return _base_label ? _base_label : name();
    }
    virtual void base_label ( const char * );

    virtual nframes_t get_module_latency ( void ) const
    {
        return 0;
    }

    virtual void get_latency ( JACK::Port::direction_e dir, nframes_t *min, nframes_t *max ) const;
    virtual void set_latency ( JACK::Port::direction_e dir, nframes_t min, nframes_t max );

    /* true if this module was added by default and not under normal user control */
    bool is_default ( void ) const
    {
        return _is_default;
    }
    void is_default ( bool v )
    {
        _is_default = v;
    }

    bool is_jack_module ( void ) const
    {
        return _is_jack_module;
    }
    void is_jack_module ( bool v )
    {
        _is_jack_module = v;
    }

    bool is_zero_input_synth ( void ) const
    {
        return  _is_zero_synth;
    }
    void is_zero_input_synth ( bool v )
    {
        _is_zero_synth = v;
    }

    /* Flag to indicate that the Module_Parameter_Editor OSC path needs update */
    bool has_name_change ( void )
    {
        return _has_name_change;
    }
    void has_name_change ( bool v )
    {
        _has_name_change = v;
    }

    virtual void update ( void ) {}

    virtual void update_tooltip ( void );

    struct Picked
    {
        unsigned int plugin_type;   // LADSPA, LV2, etc
        std::string s_unique_id;
        unsigned long unique_id;
        std::string s_plug_path;      // CLAP, VST3 binary path
    };

#ifdef LV2_SUPPORT
    bool _b_have_visible_atom_control_port;
    struct EnumeratorScalePoints
    {
        std::string Label;
        float Value;

        static bool before( const EnumeratorScalePoints& c1, const EnumeratorScalePoints& c2 )
        {
            return c1.Value < c2.Value;
        }

        EnumeratorScalePoints() : Value(0.0) {}
    };
#endif

    class Port
    {

        /* support multiple connection for audio ports (many to one, one to many, many to many).
           control ports only support one to one and one to many. */


        /* char *type_names[] = { "Audio", "Control" }; */
        /* char *direction_names[] = { "Input", "Output" }; */

        void update_connected_port_buffer ( void )
        {
            if ( connected() )
                connected_port()->_buf = _buf;
        }


    public:

        enum Direction { INPUT, OUTPUT };
        enum Type { AUDIO, CONTROL, AUX_AUDIO, ATOM, MIDI };

        /* hints for control ports (specifically control inputs) */
        struct Hints
        {
            enum Type { LINEAR, LOGARITHMIC, BOOLEAN, INTEGER, LV2_INTEGER, LV2_INTEGER_ENUMERATION, PATCH_MESSAGE };

            Type type;
            bool ranged;
            float minimum;
            float maximum;
            float default_value;
            float current_value;
            int dimensions;
            bool visible;
            bool invisible_with_signals;
            uint32_t plug_port_index;     ///< plugin Index of port control
#if defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)
            uint32_t parameter_id;
#endif
#ifdef LV2_SUPPORT
            std::vector<EnumeratorScalePoints>ScalePoints;
#endif

            Hints ( ) :
                type(LINEAR),
                ranged(false),
                minimum(0),
                maximum(1),
                default_value(0.0f),
                current_value(0.0f),
                dimensions(1),
                visible(true),
                invisible_with_signals(false),
                plug_port_index(C_MAX_UINT32)
#if defined CLAP_SUPPORT || defined(VST3_SUPPORT)
                ,parameter_id(C_MAX_UINT32) // set to max value rather than 0 because we ran into
                // a parameter VST3 that was 0 and this was default for
                // the bypass port. Kept turning the plugin on and off...
#endif
            { }
        };

        static int osc_control_change_exact ( float v, void *user_data );
        static int osc_control_change_cv ( float v, void *user_data );
        static int osc_control_update_signals ( void *user_data );

        Hints hints;

        Port ( Module *module, Direction direction, Type type, const char *name = 0 ) :
            _module(module),
            _direction(direction),
            _type(type),
            _name(name),
            _symbol(),
            _buf(0),
            _nframes(0),
            _jack_port(0),
            _scaled_signal(0),
            _unscaled_signal(0),
            _pending_feedback(false),
            _feedback_milliseconds(0),
            _by_number_number(-1),
            _by_number_path(0)
#ifdef LV2_SUPPORT
            ,_evbuf(NULL),
            _property(NULL),
            _lilv_symbol(NULL),
            _lilv_label(NULL),
            _clear_input_buffer(false),
            _need_file_update(false),
            _supports_time_position(false)
#endif
        {
            /* On port creation, we set _symbol to name for internal/LADSPA plugins that don't
               have a symbol for OSC path. For LV2/CLAP we replace after creation with the actual symbol */
            if(name)
            {
                _symbol = name;
            }
        }

        Port ( const Port& p ) :
            hints(p.hints),
            _module(p._module),
            _direction(p._direction),
            _type(p._type),
            _name(p._name),
            _symbol(p._symbol),
            _buf(p._buf),
            _nframes(p._nframes),
            _jack_port(p._jack_port),
            _scaled_signal(p._scaled_signal),
            _unscaled_signal(p._unscaled_signal),
            _pending_feedback(false),
            _feedback_milliseconds(0),
            _by_number_number(-1),
            _by_number_path(0)
#ifdef LV2_SUPPORT
            ,_evbuf(p._evbuf),
            _property(p._property),
            _lilv_symbol(p._lilv_symbol),
            _lilv_label(p._lilv_label),
            _clear_input_buffer(false),
            _need_file_update(false),
            _supports_time_position(p._supports_time_position)
#endif
        {
#ifdef LV2_SUPPORT
            hints.ScalePoints = p.hints.ScalePoints;
#endif
        }

        virtual ~Port ( )
        {
            /* FIXME: will this cause problems with cloning an instance? */
            disconnect();

            if ( _by_number_path )
                free( _by_number_path );
            _by_number_path = NULL;
#ifdef LV2_SUPPORT
            if ( _evbuf )
                lv2_evbuf_free( _evbuf );
            _evbuf = NULL;
#endif
        }

        nframes_t nframes ( void ) const
        {
            return _nframes;
        }
        void  nframes ( nframes_t n)
        {
            _nframes = n;
        }
        const char *name ( void ) const
        {
            return _name;
        }
        const char *symbol ( void ) const
        {
            return _symbol.c_str();
        }
        void set_symbol (const char * symbol)
        {
            ( _symbol = symbol);
        }
        Type type ( void ) const
        {
            return _type;
        }
        Direction direction ( void ) const
        {
            return _direction;
        }

        Module * module ( void ) const
        {
            return _module;
        }

        void buffer ( void *buf, nframes_t nframes )
        {
            _buf = buf;
            _nframes = nframes;
        };
        void *buffer ( void ) const
        {
            return _buf;
        }
#ifdef LV2_SUPPORT
        void event_buffer ( LV2_Evbuf * evbuf )
        {
            _evbuf = evbuf;
        }
        LV2_Evbuf* event_buffer ( void ) const
        {
            return _evbuf;
        }
#endif
        OSC::Signal *scaled_signal ( void )
        {
            return _scaled_signal;
        }
        OSC::Signal *unscaled_signal ( void )
        {
            return _unscaled_signal;
        }

        const char *osc_path ( )
        {
            if ( _scaled_signal )
                return _scaled_signal->path();
            else
                return NULL;
        }

        const char *osc_number_path ( void );

        void update_osc_port ( )
        {
//                if ( INPUT == _direction )
            change_osc_path( generate_osc_path() );
        }

        void destroy_osc_port ( )
        {
            delete _unscaled_signal;
            delete _scaled_signal;

            _unscaled_signal = _scaled_signal = NULL;
        }

        void control_value_no_callback ( float f )
        {
            /* can also be called from the OSC thread */
            if(!_module->_is_from_custom_ui)    // external uis sometimes crash this???
            {
                ASSERT( Thread::is( "UI" ) || Thread::is( "OSC" ),
                        "Function called from wrong thread! (is %s)",  Thread::current()->name() );
            }

            if ( buffer() )
            {
                *( static_cast<float*>(buffer()) ) = f;
            }
        }

        void control_value ( float f )
        {
            control_value_no_callback( f );
            _module->handle_control_changed( this );
            if ( connected() )
                connected_port()->_module->handle_control_changed( connected_port() );
        }

        float control_value ( void ) const
        {
            if ( buffer() )
                return *( static_cast<float*>(buffer()) );
            else
                return 0.0f;
        }

        bool connected ( void ) const
        {
            if ( _type == Port::AUDIO )
                /* internal audio ports are considered connected by the buffer setting */
                return _buf != 0;
            else
                /* control and external audio ports belong to a graph */
                return _connected.size() > 0;
        }

        unsigned int nconnected ( void ) const
        {
            return _connected.size();
        }

        bool connected_osc ( void ) const;

        Port *connected_port ( void ) const
        {
            return _connected.size() == 0 ? NULL : _connected.front();
        }

        Port *connected_port ( int n ) const
        {
            int j = 0;
            for ( std::list<Module::Port*>::const_iterator i = _connected.begin();
                    i != _connected.end(); ++i, ++j )
                if ( j == n )
                    return *i;

            return NULL;
        }

        void connect_to ( Port *to )
        {
            if ( _type != Port::AUX_AUDIO )
            {
                _buf = to->_buf;
            }

            if ( jack_port() && to->jack_port() )
                jack_port()->connect( to->jack_port()->jack_name() );

            if ( std::find(_connected.begin(),_connected.end(),to) == _connected.end() )
            {
                _connected.push_back(to);
                to->_connected.push_back(this);
            }
        }


        /* disconnect this port from any ports of modules belonging to strip /o/ */
        void disconnect_from_strip ( const Mixer_Strip *o );

        void connect_to ( void *buf )
        {
            ASSERT( _type == Port::CONTROL, "Operation only available for control ports" );
            _buf = buf;
            update_connected_port_buffer();
        }

        void set_buffer ( void *buf )
        {
            ASSERT( _type != Port::CONTROL, "Operation only available for audio ports" );
            _buf = buf;
        }

        void send_feedback ( bool force );

        bool connected_to ( Port *p )
        {
            return std::find( _connected.begin(), _connected.end(), p ) != _connected.end();
        }

        /* disconnect from specified port */
        void disconnect ( Port *p )
        {
            if ( ! connected_to(p) )
                return;

            if ( _type == Port::CONTROL && p->_module )
                p->_module->handle_control_disconnect( this );

            if ( jack_port() && p->jack_port() )
                jack_port()->disconnect( p->jack_port()->jack_name() );

            _connected.remove(p);
            p->_connected.remove(this);
        }

        /* disconnect from *all* connected ports */
        void disconnect ( void )
        {
            while ( _connected.size() )
                disconnect(_connected.front());
        }

        void jack_port ( JACK::Port *v )
        {
            _jack_port = v;
        }
        JACK::Port *jack_port ( void ) const
        {
            return _jack_port;
        }

        void schedule_feedback ( void )
        {
            _pending_feedback = true;
        }

    private:

        char *generate_osc_path ( void );
        void change_osc_path ( char *path );

        std::list <Port*> _connected;

        Module *_module;
        Direction _direction;
        Type _type;
        const char *_name;
        std::string _symbol;
        void *_buf;
        nframes_t _nframes;

        /* used for auxilliary I/Os */
        JACK::Port *_jack_port;

        OSC::Signal *_scaled_signal;
        OSC::Signal *_unscaled_signal;

        /* float _feedback_value; */
        bool _pending_feedback;
        unsigned long long _feedback_milliseconds;

        int _by_number_number;
        char *_by_number_path;

#ifdef LV2_SUPPORT
        LV2_Evbuf* _evbuf;
    public:
        const LilvNode* _property;
        LilvNode*   _lilv_symbol;       ///< Symbol
        LilvNode*   _lilv_label;        ///< Human readable label
        LV2_URID    _property_mapped;   ///< Iff type == PROPERTY
        bool _clear_input_buffer;
        bool _need_file_update;
        bool _supports_time_position;
        std::string _file;
    private:
#endif

        static void handle_signal_connection_state_changed ( OSC::Signal *, void *o );
    };

    virtual void bbox ( int &X, int &Y, int &W, int &H )
    {
        X += + 5;
        Y += 5;
        W -= 10;
        H -= 10;
    }

    Module ( int W, int H, const char *L = 0 );
    Module ( );
    Module ( bool is_default, int W, int H, const char *L = 0 );

    virtual ~Module ( );

    LOG_NAME_FUNC( Module );

    virtual void resize_buffers ( nframes_t v )
    {
        _nframes = v;
    }


    int instances ( void ) const
    {
        return _instances;
    }
    void instances ( int i )
    {
        _instances = i;
    }

    bool is_being_controlled ( void ) const
    {
        for ( nframes_t i = control_input.size(); i--; )
            if ( control_input[i].connected() )
                return true;
        return false;
    }

    bool is_controlling ( void ) const
    {
        for ( nframes_t i = control_output.size(); i--; )
            if ( control_output[i].connected() )
                return true;
        return false;
    }

    /* bool */
    /* is_being_controlled_osc ( void ) const */
    /*     { */
    /*         for ( nframes_t i = control_input.size(); i--; ) */
    /*             if ( control_input[i].connected_osc() ) */
    /*                 return true; */
    /*         return false; */
    /*     } */

    virtual const char *name ( void ) const = 0;
    virtual const char *basename ( void ) const
    {
        return "Module";
    }

    std::vector<Port> audio_input;
    std::vector<Port> audio_output;
    std::vector<Port> control_input;
    std::vector<Port> control_output;
    std::vector<Port> aux_audio_input;
    std::vector<Port> aux_audio_output;

    virtual void add_port ( const Port &p )
    {
        if ( p.type() == Port::AUDIO && p.direction() == Port::INPUT )
            audio_input.push_back( p );
        else if ( p.type() == Port::AUDIO && p.direction() == Port::OUTPUT )
            audio_output.push_back( p );
        else if ( p.type() == Port::CONTROL && p.direction() == Port::INPUT )
            control_input.push_back( p );
        else if ( p.type() == Port::CONTROL && p.direction() == Port::OUTPUT )
            control_output.push_back( p );
    }

    int noutputs ( void ) const
    {
        return audio_output.size();
    }

    int ninputs ( void ) const
    {
        return audio_input.size();
    }

    int ncontrol_inputs ( void ) const
    {
        return control_input.size();
    }

    int ncontrol_outputs ( void ) const
    {
        return control_output.size();
    }

    int nvisible_control_inputs ( void ) const
    {
        int n = 0;
        for ( std::vector<Port>::const_iterator i = control_input.begin();
                i != control_input.end();
                ++i )
            if ( i->hints.visible )
                n++;

        return n;
    }

    virtual bool bypass ( void ) const
    {
        return *_bypass == 1.0f;
    }
    virtual void bypass ( bool v )
    {
        *_bypass = (float)v;
    }

    virtual bool bypassable ( void ) const
    {
        return ninputs() == noutputs() ||
               ( ninputs() == 1 && noutputs() == 2 );
    }

    int control_input_port_index ( Port *p )
    {
        for ( nframes_t i = control_input.size(); i--; )
            if ( &control_input[i] == p )
                return i;

        return -1;
    }

    int control_output_port_index ( Port *p )
    {
        for ( nframes_t i = control_output.size(); i--; )
            if ( &control_output[i] == p )
                return i;

        return -1;
    }

    Chain *chain ( void ) const
    {
        return _chain;
    }
    void chain ( Chain * v );

    char *get_parameters ( void ) const;
    void set_parameters ( const char * );

    bool show_analysis_window ( void );

    void send_feedback ( bool force );
    void schedule_feedback ( void );
    virtual bool initialize ( void )
    {
        return true;
    }

    /* for the given number of inputs, return how many outputs this
     * plugin would have. -1 if this plugin can't support so many
     * inputs. */
    virtual int can_support_inputs ( int n ) = 0;
    /* called by the chain whenever we need to adjust our input
     * channel configuration, but only if can_support_inputs() returns
     * true */
    virtual bool configure_inputs ( int n ) = 0;

    virtual void configure_midi_inputs () {}
    virtual void configure_midi_outputs () {}

    virtual void process ( nframes_t ) = 0;

    /* called whenever the module is initialized or when the sample rate is changed at runtime */
    virtual void handle_sample_rate_change ( nframes_t /*sample_rate*/ ) {}

    /* called whenever the value of a control port is changed.
       This can be used to take appropriate action from the GUI thread */
    virtual void handle_control_changed ( Port * );
    virtual void handle_control_disconnect ( Port * ) {}
    /* called whenever the name of the chain changes (usually because
     * the name of the mixer strip changed). */
    virtual void handle_chain_name_changed ();

    virtual void handle_port_connection_change () {}

    /* module should create a new context, run against this impulse,
     * and return true if there's anything worth reporting */
    virtual bool get_impulse_response ( sample_t * /*buf*/, nframes_t /*nframes*/ )
    {
        return false;
    }

#define MODULE_CLONE_FUNC(class)                                \
    virtual Module *clone_empty ( void ) const override         \
        {                                                       \
            return new class ();                                \
        }

    virtual Module *clone_empty ( void ) const
    {
        return NULL;
    }
    Module *clone ( Chain *dest ) const;
    Module *clone ( void ) const;

protected:

    void draw_connections ( void );
    void draw_label ( int X, int Y, int W, int H );
    void draw_box ( int X, int Y, int W, int H );

    virtual void draw ( void )
    {
        Module::draw_box(x(),y(),w(),h());
        Module::draw_label(x(),y(),w(),h());
    }
    virtual int handle ( int m );

    virtual void get ( Log_Entry &e ) const override;
    virtual void set ( Log_Entry &e ) override;

    bool add_aux_port ( bool input, const char *prefix, int n, JACK::Port::type_e type );

public:

    nframes_t buffer_size ( void ) const
    {
        return Module::_buffer_size;
    }
    static nframes_t sample_rate ( void )
    {
        return Module::_sample_rate;
    }
    nframes_t nframes ( void )
    {
        return _nframes;
    }


    void auto_connect_outputs();
    void auto_disconnect_outputs();

    virtual void freeze_ports ( void );
    virtual void thaw_ports ( void );
    virtual void clear_midi_vectors() {};

    void destroy_connected_controller_module();

    bool add_aux_audio_output ( const char *prefix, int n );
    bool add_aux_audio_input ( const char *prefix, int n );
    bool add_aux_cv_input ( const char *prefix, int n );

    static void set_buffer_size ( nframes_t bsize )
    {
        _buffer_size = bsize;
    }
    static void sample_rate ( nframes_t srate )
    {
        _sample_rate = srate;
    }

    void command_open_parameter_editor();
    void open_plugin_ui();
    virtual void command_activate ( void );
    virtual void command_deactivate ( void );
    virtual void command_remove ( void );

};
