/*******************************************************************************/
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


/* 
 * File:   VST3_Plugin.C
 * Author: sspresto
 * 
 * Created on December 20, 2023, 9:24 AM
 */

#ifdef VST3_SUPPORT

#include <filesystem>
#include <dlfcn.h>      // dlopen, dlerror, dlsym
#include <unordered_map>

#include "../../../nonlib/dsp.h"
#include "VST3_Plugin.H"
#include "../Chain.H"

#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"

#define ASRT_TMP ASSERT
#undef ASSERT       // Fix redefinition with /nonlib/debug.h"
#undef WARNING      // Fix redefinition with /nonlib/debug.h"
#include "base/source/fobject.h"
#undef ASSERT       // Fix redefinition with /nonlib/debug.h"
#undef WARNING      // Fix redefinition with /nonlib/debug.h"

#define ASSERT ASRT_TMP
#define WARNING( fmt, args... ) warnf( W_WARNING, __MODULE__, __FILE__, __FUNCTION__, __LINE__, fmt, ## args )

const unsigned char  EVENT_NOTE_OFF         = 0x80;
const unsigned char  EVENT_NOTE_ON          = 0x90;
const unsigned char  EVENT_CHANNEL_PRESSURE = 0xa0;

using namespace Steinberg;
using namespace Linux;


std::string utf16_to_utf8(const u16string& utf16)
{
    wstring_convert<codecvt_utf8_utf16<char16_t>,char16_t> convert; 
    string utf8 = convert.to_bytes(utf16);
    return utf8;
}

//-----------------------------------------------------------------------------
// class qtractorVst3PluginHost -- VST3 plugin host context decl.
//

class qtractorVst3PluginHost : public Vst::IHostApplication
{
public:

	// Constructor.
	qtractorVst3PluginHost ();

	// Destructor.
	virtual ~qtractorVst3PluginHost ();

	DECLARE_FUNKNOWN_METHODS

	//--- IHostApplication ---
	//
	tresult PLUGIN_API getName (Vst::String128 name) override;
	tresult PLUGIN_API createInstance (TUID cid, TUID _iid, void **obj) override;

	FUnknown *get() { return static_cast<Vst::IHostApplication *> (this); }

	// QTimer stuff...
	//
	void startTimer (int msecs);
	void stopTimer ();

	int timerInterval() const;

	// RunLoop adapters...
	//
	tresult registerEventHandler (IEventHandler *handler, FileDescriptor fd);
	tresult unregisterEventHandler (IEventHandler *handler);

	tresult registerTimer (ITimerHandler *handler, TimerInterval msecs);
	tresult unregisterTimer (ITimerHandler *handler);

	// Executive methods.
	//
	void processTimers();
	void processEventHandlers();

#ifdef CONFIG_VST3_XCB
	void openXcbConnection();
	void closeXcbConnection();
#endif

	// Common host time-keeper context accessors.
	Vst::ProcessContext *processContext();

	void processAddRef();
	void processReleaseRef();

	// Common host time-keeper process context.
	void updateProcessContext(jack_position_t &pos, const bool &xport_changed, const bool &has_bbt);

	// Cleanup.
	void clear();

protected:

    class PlugInterfaceSupport;

    class Attribute;
    class AttributeList;
    class Message;

 //   class Timer;  // FIXME

private:

    // Instance members.
    IPtr<PlugInterfaceSupport> m_plugInterfaceSupport;

//	Timer *m_pTimer;    // FIXME

    unsigned int m_timerRefCount;

    struct TimerHandlerItem
    {
            TimerHandlerItem(ITimerHandler *h, TimerInterval i)
                    : handler(h), interval(i), counter(0) {}

            void reset(TimerInterval i)
                    { interval = i; counter = 0; }

            ITimerHandler *handler;
            TimerInterval  interval;
            TimerInterval  counter;
    };

    std::unordered_map<int, double> m_timerHandlers;
    //QHash<ITimerHandler *, TimerHandlerItem *> m_timerHandlers;

    std::list<TimerHandlerItem *> m_timerHandlerItems;
    //QList<TimerHandlerItem *> m_timerHandlerItems;

    std::unordered_map<IEventHandler *, int> m_eventHandlers;
    //QMultiHash<IEventHandler *, int> m_eventHandlers;

#ifdef CONFIG_VST3_XCB
    xcb_connection_t *m_pXcbConnection;
    int               m_iXcbFileDescriptor;
#endif

    Vst::ProcessContext m_processContext;
    unsigned int        m_processRefCount;
};


//-----------------------------------------------------------------------------
//
class qtractorVst3PluginHost::PlugInterfaceSupport
	: public FObject, public Vst::IPlugInterfaceSupport
{
public:

	// Constructor.
	PlugInterfaceSupport ()
	{
		addPluInterfaceSupported(Vst::IComponent::iid);
		addPluInterfaceSupported(Vst::IAudioProcessor::iid);
		addPluInterfaceSupported(Vst::IEditController::iid);
		addPluInterfaceSupported(Vst::IConnectionPoint::iid);
		addPluInterfaceSupported(Vst::IUnitInfo::iid);
	//	addPluInterfaceSupported(Vst::IUnitData::iid);
		addPluInterfaceSupported(Vst::IProgramListData::iid);
		addPluInterfaceSupported(Vst::IMidiMapping::iid);
	//	addPluInterfaceSupported(Vst::IEditController2::iid);
	}

	OBJ_METHODS (PlugInterfaceSupport, FObject)
	REFCOUNT_METHODS (FObject)
	DEFINE_INTERFACES
		DEF_INTERFACE (Vst::IPlugInterfaceSupport)
	END_DEFINE_INTERFACES (FObject)

	//--- IPlugInterfaceSupport ----
	//
	tresult PLUGIN_API isPlugInterfaceSupported (const TUID _iid) override
	{
            //if (m_fuids.contains(QString::fromLocal8Bit(_iid)))
            for( unsigned i = 0; i < m_fuids.size(); ++i)
            {
                if ( strcmp(_iid, m_fuids[i].c_str() ) == 0)
                    return kResultOk;
            }
	//	else
            return kResultFalse;
	}

protected:

	void addPluInterfaceSupported(const TUID& _iid)
            { m_fuids.push_back(_iid); }
	//	{ m_fuids.append(QString::fromLocal8Bit(_iid)); }

private:

	// Instance members.
	std::vector<std::string> m_fuids;
};


//-----------------------------------------------------------------------------
//
class qtractorVst3PluginHost::Attribute
{
public:

	enum Type
	{
		kInteger,
		kFloat,
		kString,
		kBinary
	};

	// Constructors.
	Attribute (int64 value) : m_size(0), m_type(kInteger)
		{ m_v.intValue = value; }

	Attribute (double value) : m_size(0), m_type(kFloat)
		{ m_v.floatValue = value; }

	Attribute (const Vst::TChar *value, uint32 size)
		: m_size(size), m_type(kString)
	{
		m_v.stringValue = new Vst::TChar[size];
		::memcpy(m_v.stringValue, value, size * sizeof (Vst::TChar));
	}

	Attribute (const void *value, uint32 size)
		: m_size(size), m_type(kBinary)
	{
		m_v.binaryValue = new char[size];
		::memcpy(m_v.binaryValue, value, size);
	}

	// Destructor.
	~Attribute ()
	{
		if (m_size)
			delete [] m_v.binaryValue;
	}

	// Accessors.
	int64 intValue () const
		{ return m_v.intValue; }

	double floatValue () const
		{ return m_v.floatValue; }

	const Vst::TChar *stringValue ( uint32& stringSize )
	{
		stringSize = m_size;
		return m_v.stringValue;
	}

	const void *binaryValue ( uint32& binarySize )
	{
		binarySize = m_size;
		return m_v.binaryValue;
	}

	Type getType () const
		{ return m_type; }

protected:

	// Instance members.
	union v
	{
		int64  intValue;
		double floatValue;
		Vst::TChar *stringValue;
		char  *binaryValue;

	} m_v;

	uint32 m_size;
	Type m_type;
};


//-----------------------------------------------------------------------------
//
class qtractorVst3PluginHost::AttributeList : public Vst::IAttributeList
{
public:

	// Constructor.
	AttributeList ()
	{
		FUNKNOWN_CTOR
	}

	// Destructor.
	virtual ~AttributeList ()
	{
            for (auto i : m_list)
            {
                delete i.second;
            }
	//	qDeleteAll(m_list);
            m_list.clear();

            FUNKNOWN_DTOR
	}

	DECLARE_FUNKNOWN_METHODS

	//--- IAttributeList ---
	//
	tresult PLUGIN_API setInt (AttrID aid, int64 value) override
	{   
            removeAttrID(aid);

            std::pair<std::string, Attribute *> Attr ( aid, new Attribute(value) );
            m_list.insert(Attr);
            //	m_list.insert(aid, new Attribute(value));
            return kResultTrue;
	}

	tresult PLUGIN_API getInt (AttrID aid, int64& value) override
	{
            std::unordered_map<std::string, Attribute *>::const_iterator got
                = m_list.find (aid);
            
            if ( got == m_list.end() )
            {
                return kResultFalse;
            }
            
            Attribute *attr = got->second;
	//	Attribute *attr = m_list.value(aid, nullptr);
            if (attr)
            {
                value = attr->intValue();
                return kResultTrue;
            }

            return kResultFalse;
	}

	tresult PLUGIN_API setFloat (AttrID aid, double value) override
	{
            removeAttrID(aid);

            std::pair<std::string, Attribute *> Attr ( aid, new Attribute(value) );
            m_list.insert(Attr);

          //  m_list.insert(aid, new Attribute(value));
            return kResultTrue;
	}

	tresult PLUGIN_API getFloat (AttrID aid, double& value) override
	{
            std::unordered_map<std::string, Attribute *>::const_iterator got
                = m_list.find (aid);
            
            if ( got == m_list.end() )
            {
                return kResultFalse;
            }
            
            Attribute *attr = got->second;
            
         //   Attribute *attr = m_list.value(aid, nullptr);
            if (attr)
            {
                value = attr->floatValue();
                return kResultTrue;
            }

            return kResultFalse;
	}

	tresult PLUGIN_API setString (AttrID aid, const Vst::TChar *string) override
	{
#if 0
            removeAttrID(aid);
            m_list.insert(aid, new Attribute(string, fromTChar(string).length()));
            return kResultTrue;
#endif
            return kResultFalse;    // FIXME
	}

	tresult PLUGIN_API getString (AttrID aid, Vst::TChar *string, uint32 size) override
	{
#if 0
            Attribute *attr = m_list.value(aid, nullptr);
            if (attr) {
                    uint32 size2 = 0;
                    const Vst::TChar *string2 = attr->stringValue(size2);
                    ::memcpy(string, string2, qMin(size, size2) * sizeof(Vst::TChar));
                    return kResultTrue;
            }
#endif
            return kResultFalse;    // FIXME
	}

	tresult PLUGIN_API setBinary (AttrID aid, const void* data, uint32 size) override
	{
            removeAttrID(aid);

            std::pair<std::string, Attribute *> Attr ( aid, new Attribute(data, size) );
            m_list.insert(Attr);
            
          //  m_list.insert(aid, new Attribute(data, size));
            return kResultTrue;
	}

	tresult PLUGIN_API getBinary (AttrID aid, const void*& data, uint32& size) override
	{
            std::unordered_map<std::string, Attribute *>::const_iterator got
                = m_list.find (aid);
            
            if ( got == m_list.end() )
            {
                return kResultFalse;
            }
            
            Attribute *attr = got->second;
           // Attribute *attr = m_list.value(aid, nullptr);
            if (attr) {
                    data = attr->binaryValue(size);
                    return kResultTrue;
            }
            size = 0;
            return kResultFalse;
	}

protected:

	void removeAttrID (AttrID aid)
	{
            std::unordered_map<std::string, Attribute *>::const_iterator got
                = m_list.find (aid);
            
            if ( got == m_list.end() )
            {
                return;
            }

            Attribute *attr = got->second;
          //  Attribute *attr = m_list.value(aid, nullptr);
            if (attr)
            {
                delete attr;
                m_list.erase(aid);
              //  m_list.remove(aid);
            }
	}

private:

	// Instance members.
        std::unordered_map<std::string, Attribute *> m_list;
	//QHash<QString, Attribute *> m_list;
};

IMPLEMENT_FUNKNOWN_METHODS (qtractorVst3PluginHost::AttributeList, IAttributeList, IAttributeList::iid)


//-----------------------------------------------------------------------------
//
class qtractorVst3PluginHost::Message : public Vst::IMessage
{
public:

	// Constructor.
	Message () : m_messageId(nullptr), m_attributeList(nullptr)
	{
		FUNKNOWN_CTOR
	}

	// Destructor.
	virtual ~Message ()
	{
		setMessageID(nullptr);

		if (m_attributeList)
			m_attributeList->release();

		FUNKNOWN_DTOR
	}

	DECLARE_FUNKNOWN_METHODS

	//--- IMessage ---
	//
	const char *PLUGIN_API getMessageID () override
		{ return m_messageId; }

	void PLUGIN_API setMessageID (const char *messageId) override
	{
		if (m_messageId)
			delete [] m_messageId;

		m_messageId = nullptr;

		if (messageId) {
			size_t len = strlen(messageId) + 1;
			m_messageId = new char[len];
			::strcpy(m_messageId, messageId);
		}
	}

	Vst::IAttributeList* PLUGIN_API getAttributes () override
	{
		if (!m_attributeList)
			m_attributeList = new AttributeList();

		return m_attributeList;
	}

protected:

	// Instance members.
	char *m_messageId;

	AttributeList *m_attributeList;
};

IMPLEMENT_FUNKNOWN_METHODS (qtractorVst3PluginHost::Message, IMessage, IMessage::iid)


//-----------------------------------------------------------------------------
// class qtractorVst3PluginHost::Timer -- VST3 plugin host timer impl.
//

#if 0
class qtractorVst3PluginHost::Timer : public QTimer
{
public:

	// Constructor.
	Timer (qtractorVst3PluginHost *pHost) : QTimer(), m_pHost(pHost) {}

	// Main method.
	void start (int msecs)
	{
		const int DEFAULT_MSECS = 30;

		int iInterval = QTimer::interval();
		if (iInterval == 0)
			iInterval = DEFAULT_MSECS;
		if (iInterval > msecs)
			iInterval = msecs;

		QTimer::start(iInterval);
	}

protected:

	void timerEvent (QTimerEvent *pTimerEvent)
	{
		if (pTimerEvent->timerId() == QTimer::timerId()) {
			m_pHost->processTimers();
			m_pHost->processEventHandlers();
		}
	}

private:

	// Instance members.
	qtractorVst3PluginHost *m_pHost;
};
#endif

//-----------------------------------------------------------------------------
// class qtractorVst3PluginHost -- VST3 plugin host context impl.
//

// Constructor.
qtractorVst3PluginHost::qtractorVst3PluginHost (void)
{
    FUNKNOWN_CTOR

    m_plugInterfaceSupport = owned(NEW PlugInterfaceSupport());

//	m_pTimer = new Timer(this); // FIXME

    m_timerRefCount = 0;

#ifdef CONFIG_VST3_XCB
    m_pXcbConnection = nullptr;
    m_iXcbFileDescriptor = 0;
#endif

    m_processRefCount = 0;
}


// Destructor.
qtractorVst3PluginHost::~qtractorVst3PluginHost (void)
{
    clear();

//    delete m_pTimer;  // FIXME

    m_plugInterfaceSupport = nullptr;

    FUNKNOWN_DTOR
}



//--- IHostApplication ---
//
tresult PLUGIN_API qtractorVst3PluginHost::getName ( Vst::String128 name )
{
    const std::string str("qtractorVst3PluginHost");
    const int nsize = str.length() < 127 ? str.length() : 127;
//    const QString str("qtractorVst3PluginHost");
//    const int nsize = qMin(str.length(), 127);
    ::memcpy(name, str.c_str(), nsize * sizeof(Vst::TChar));
    name[nsize] = 0;
    return kResultOk;
}


tresult PLUGIN_API qtractorVst3PluginHost::createInstance (
	TUID cid, TUID _iid, void **obj )
{
	const FUID classID (FUID::fromTUID(cid));
	const FUID interfaceID (FUID::fromTUID(_iid));

	if (classID == Vst::IMessage::iid &&
		interfaceID == Vst::IMessage::iid) {
		*obj = new Message();
		return kResultOk;
	}
	else
	if (classID == Vst::IAttributeList::iid &&
		interfaceID == Vst::IAttributeList::iid) {
		*obj = new AttributeList();
		return kResultOk;
	}

	*obj = nullptr;
	return kResultFalse;
}


tresult PLUGIN_API qtractorVst3PluginHost::queryInterface (
	const char *_iid, void **obj )
{
	QUERY_INTERFACE(_iid, obj, FUnknown::iid, IHostApplication)
	QUERY_INTERFACE(_iid, obj, IHostApplication::iid, IHostApplication)

	if (m_plugInterfaceSupport &&
		m_plugInterfaceSupport->queryInterface(_iid, obj) == kResultOk)
		return kResultOk;

	*obj = nullptr;
	return kResultFalse;
}


uint32 PLUGIN_API qtractorVst3PluginHost::addRef (void)
	{ return 1;	}

uint32 PLUGIN_API qtractorVst3PluginHost::release (void)
	{ return 1; }


// QTimer stuff...
//
#if 0
void qtractorVst3PluginHost::startTimer ( int msecs )
	{ if (++m_timerRefCount == 1) m_pTimer->start(msecs); }

void qtractorVst3PluginHost::stopTimer (void)
	{ if (m_timerRefCount > 0 && --m_timerRefCount == 0) m_pTimer->stop(); }

int qtractorVst3PluginHost::timerInterval (void) const
	{ return m_pTimer->interval(); }
#endif

// IRunLoop stuff...
//
tresult qtractorVst3PluginHost::registerEventHandler (
	IEventHandler *handler, FileDescriptor fd )
{
#ifdef CONFIG_DEBUG
    qDebug("qtractorVst3PluginHost::registerEventHandler(%p, %d)", handler, int(fd));
#endif
    std::pair<IEventHandler *, int> Attr ( handler, int(fd) );
    m_eventHandlers.insert(Attr);

    //m_eventHandlers.insert(handler, int(fd));
    return kResultOk;
}


tresult qtractorVst3PluginHost::unregisterEventHandler ( IEventHandler *handler )
{
#ifdef CONFIG_DEBUG
    qDebug("qtractorVst3PluginHost::unregisterEventHandler(%p)", handler);
#endif

    m_eventHandlers.erase(handler);
    
   // m_eventHandlers.remove(handler);
    return kResultOk;
}

#if 1
tresult qtractorVst3PluginHost::registerTimer (
	ITimerHandler *handler, TimerInterval msecs )
{
    DMESSAGE("registerTimer(%p, %u)", handler, uint(msecs));
#if 0
    TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
    if (timer_handler) {
            timer_handler->reset(msecs);
    } else {
            timer_handler = new TimerHandlerItem(handler, msecs);
            m_timerHandlers.insert(handler, timer_handler);
    }

    m_pTimer->start(int(msecs));
#endif
    return kResultOk;
}


tresult qtractorVst3PluginHost::unregisterTimer ( ITimerHandler *handler )
{
    DMESSAGE("unregisterTimer(%p)", handler);
#if 0
    TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
    if (timer_handler) {
            m_timerHandlers.remove(handler);
            m_timerHandlerItems.append(timer_handler);
    }
    if (m_timerHandlers.isEmpty())
            m_pTimer->stop();

#endif
    return kResultOk;
}

// Executive methods.
//
void qtractorVst3PluginHost::processTimers (void)
{
#if 0
    foreach (TimerHandlerItem *timer_handler, m_timerHandlers) {
            timer_handler->counter += timerInterval();
            if (timer_handler->counter >= timer_handler->interval) {
                    timer_handler->handler->onTimer();
                    timer_handler->counter = 0;
            }
    }
#endif
}
#endif

void qtractorVst3PluginHost::processEventHandlers (void)
{
    int nfds = 0;

    fd_set rfds;
    fd_set wfds;
    fd_set efds;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

#ifdef CONFIG_VST3_XCB
    if (m_iXcbFileDescriptor) {
            FD_SET(m_iXcbFileDescriptor, &rfds);
            FD_SET(m_iXcbFileDescriptor, &wfds);
            FD_SET(m_iXcbFileDescriptor, &efds);
            nfds = qMax(nfds, m_iXcbFileDescriptor);
    }
#endif

  //  std::unordered_map<IEventHandler *, int> m_eventHandlers;
    
    for (auto const& pair : m_eventHandlers)
    {
        auto fd = pair.second;
        FD_SET(fd, &rfds);
        FD_SET(fd, &wfds);
        FD_SET(fd, &efds);
        nfds = nfds > fd ? nfds : fd;
    }

#if 0
    QMultiHash<IEventHandler *, int>::ConstIterator iter
            = m_eventHandlers.constBegin();
    for ( ; iter != m_eventHandlers.constEnd(); ++iter) {
            foreach (int fd, m_eventHandlers.values(iter.key())) {
                    FD_SET(fd, &rfds);
                    FD_SET(fd, &wfds);
                    FD_SET(fd, &efds);
                    nfds = qMax(nfds, fd);
            }
    }

    timeval timeout;
    timeout.tv_sec  = 0;
    timeout.tv_usec = 1000 * timerInterval();

    const int result = ::select(nfds, &rfds, &wfds, nullptr, &timeout);
    if (result > 0)	{
            iter = m_eventHandlers.constBegin();
            for ( ; iter != m_eventHandlers.constEnd(); ++iter) {
                    foreach (int fd, m_eventHandlers.values(iter.key())) {
                            if (FD_ISSET(fd, &rfds) ||
                                    FD_ISSET(fd, &wfds) ||
                                    FD_ISSET(fd, &efds)) {
                                    IEventHandler *handler = iter.key();
                                    handler->onFDIsSet(fd);
                            }
                    }
            }
    }
#endif
}


#ifdef CONFIG_VST3_XCB

void qtractorVst3PluginHost::openXcbConnection (void)
{
	if (m_pXcbConnection == nullptr) {
	#ifdef CONFIG_DEBUG
		qDebug("qtractorVst3PluginHost::openXcbConnection()");
	#endif
		m_pXcbConnection = ::xcb_connect(nullptr, nullptr);
		m_iXcbFileDescriptor = ::xcb_get_file_descriptor(m_pXcbConnection);
	}
}

void qtractorVst3PluginHost::closeXcbConnection (void)
{
	if (m_pXcbConnection) {
		::xcb_disconnect(m_pXcbConnection);
		m_pXcbConnection = nullptr;
		m_iXcbFileDescriptor = 0;
	#ifdef CONFIG_DEBUG
		qDebug("qtractorVst3PluginHost::closeXcbConnection()");
	#endif
	}
}

#endif	// CONFIG_VST3_XCB


// Common host time-keeper context accessor.
Vst::ProcessContext *qtractorVst3PluginHost::processContext (void)
{
	return &m_processContext;
}


void qtractorVst3PluginHost::processAddRef (void)
{
	++m_processRefCount;
}


void qtractorVst3PluginHost::processReleaseRef (void)
{
	if (m_processRefCount > 0) --m_processRefCount;
}

// Common host time-keeper process context.
void qtractorVst3PluginHost::updateProcessContext (
	jack_position_t &pos, const bool &xport_changed, const bool &has_bbt )
{
    if (m_processRefCount < 1)
        return;

    if (xport_changed)
        m_processContext.state |=  Vst::ProcessContext::kPlaying;
    else
        m_processContext.state &= ~Vst::ProcessContext::kPlaying;
    
    if(has_bbt)
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;

        m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid;
        m_processContext.projectTimeMusic = pos.beat;
        m_processContext.state |= Vst::ProcessContext::kBarPositionValid;
        m_processContext.barPositionMusic = pos.bar;

        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo  = pos.beats_per_minute;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = pos.beats_per_bar;
        m_processContext.timeSigDenominator = pos.beat_type;
    }
    else
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;
        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo  = 120.0;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = 4;
        m_processContext.timeSigDenominator = 4;
    }
}

// Cleanup.
void qtractorVst3PluginHost::clear (void)
{
#ifdef CONFIG_VST3_XCB
    closeXcbConnection();
#endif

    m_timerRefCount = 0;
    m_processRefCount = 0;

    for (auto i : m_timerHandlerItems)
    {
        delete i;
    }

//    qDeleteAll(m_timerHandlerItems);
    m_timerHandlerItems.clear();
    m_timerHandlers.clear();

    m_eventHandlers.clear();

    ::memset(&m_processContext, 0, sizeof(Vst::ProcessContext));
}


// Host singleton.
static qtractorVst3PluginHost g_hostContext;

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::ParamQueue, Vst::IParamValueQueue, Vst::IParamValueQueue::iid)

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::ParamChanges, Vst::IParameterChanges, Vst::IParameterChanges::iid)

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::EventList, IEventList, IEventList::iid)

//----------------------------------------------------------------------
// class VST3_Plugin::Handler -- VST3 plugin interface handler.
// Plugin uses this to send messages, update to the host. Plugin to host.

class VST3_Plugin::Handler
    : public Vst::IComponentHandler
    , public Vst::IConnectionPoint
{
public:

    // Constructor.
    Handler (VST3_Plugin *pPlugin)
            : m_pPlugin(pPlugin) { FUNKNOWN_CTOR }

    // Destructor.
    virtual ~Handler () { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IComponentHandler ---
    //
    tresult PLUGIN_API beginEdit (Vst::ParamID id) override
    {
        DMESSAGE("Handler[%p]::beginEdit(%d)", this, int(id));
        return kResultOk;
    }

    tresult PLUGIN_API performEdit (Vst::ParamID id, Vst::ParamValue value) override
    {
        DMESSAGE("Handler[%p]::performEdit(%d, %g)", this, int(id), float(value));

        m_pPlugin->setParameter(id, value, 0);

#if 1
        unsigned long index = m_pPlugin->findParamId(int(id));

        // false means don't update custom UI cause that is were it came from
        m_pPlugin->set_control_value( index, value, false );
        
#else
        qtractorPlugin::Param *pParam = m_pPlugin->findParamId(int(id));
        if (pParam)
            pParam->updateValue(float(value), false);
#endif
        return kResultOk;
    }

    tresult PLUGIN_API endEdit (Vst::ParamID id) override
    {
        DMESSAGE("Handler[%p]::endEdit(%d)", this, int(id));
        return kResultOk;
    }

    tresult PLUGIN_API restartComponent (int32 flags) override
    {
        DMESSAGE("Handler[%p]::restartComponent(0x%08x)", this, flags);

        if (flags & Vst::kParamValuesChanged)
            m_pPlugin->updateParamValues(false);
        else
        if (flags & Vst::kReloadComponent)
        {
            m_pPlugin->deactivate();
            m_pPlugin->activate();
        }

        return kResultOk;
    }

    //--- IConnectionPoint ---
    //
    tresult PLUGIN_API connect (Vst::IConnectionPoint *other) override
            { return (other ? kResultOk : kInvalidArgument); }

    tresult PLUGIN_API disconnect (Vst::IConnectionPoint *other) override
            { return (other ? kResultOk : kInvalidArgument); }

    tresult PLUGIN_API notify (Vst::IMessage *message) override
    {
        return m_pPlugin->notify(message);
    }

private:

    // Instance client.
    VST3_Plugin *m_pPlugin;
};

tresult PLUGIN_API VST3_Plugin::Handler::queryInterface (
	const char *_iid, void **obj )
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IConnectionPoint::iid, IConnectionPoint)

    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API VST3_Plugin::Handler::addRef (void)
    { return 1000; }

uint32 PLUGIN_API VST3_Plugin::Handler::release (void)
    { return 1000; }

//----------------------------------------------------------------------
// class VST3_Plugin::RunLoop -- VST3 plugin editor run-loop impl.
//

class VST3_Plugin::RunLoop : public IRunLoop
{
public:

	//--- IRunLoop ---
	//
	tresult PLUGIN_API registerEventHandler (IEventHandler *handler, FileDescriptor fd) override
		{ return g_hostContext.registerEventHandler(handler, fd); }

	tresult PLUGIN_API unregisterEventHandler (IEventHandler *handler) override
		{ return g_hostContext.unregisterEventHandler(handler); }

	tresult PLUGIN_API registerTimer (ITimerHandler *handler, TimerInterval msecs) override
		{ return g_hostContext.registerTimer(handler, msecs); }

	tresult PLUGIN_API unregisterTimer (ITimerHandler *handler) override
		{ return g_hostContext.unregisterTimer(handler); }

	tresult PLUGIN_API queryInterface (const TUID _iid, void **obj) override
	{
		if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
			FUnknownPrivate::iidEqual(_iid, IRunLoop::iid)) {
			addRef();
			*obj = this;
			return kResultOk;
		}

		*obj = nullptr;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef  () override { return 1001; }
	uint32 PLUGIN_API release () override { return 1001; }
};


//----------------------------------------------------------------------
// class qtractorVst3Plugin::EditorFrame -- VST3 plugin editor frame interface impl.
//

class VST3_Plugin::EditorFrame : public IPlugFrame
{
public:

    // Constructor.
    EditorFrame (IPlugView *plugView, X11PluginUI *widget)
            : m_plugView(plugView), m_widget(widget),
                    m_runLoop(nullptr), m_resizing(false)
    {
        m_runLoop = owned(NEW RunLoop());
        m_plugView->setFrame(this);

        ViewRect rect;
        if (m_plugView->getSize(&rect) == kResultOk)
        {
            m_resizing = true;
          //  const QSize size( rect.right  - rect.left, rect.bottom - rect.top);
          //  m_widget->resize(size);
            m_widget->setSize(rect.right - rect.left, rect.bottom - rect.top, false, false);
            m_resizing = false;
        }
    }

    // Destructor.
    virtual ~EditorFrame ()
    {
        m_plugView->setFrame(nullptr);
        m_runLoop = nullptr;
    }

    // Accessors.
    IPlugView *plugView () const
            { return m_plugView; }
    RunLoop *runLoop () const
            { return m_runLoop; }

    //--- IPlugFrame ---
    //
    tresult PLUGIN_API resizeView (IPlugView *plugView, ViewRect *rect) override
    {
        if (!rect || !plugView || plugView != m_plugView)
                return kInvalidArgument;

        if (!m_widget)
            return kInternalError;
        if (m_resizing)
            return kResultFalse;

        m_resizing = true;
#if 1
        int width = rect->right  - rect->left;
        int height = rect->bottom - rect->top;
        DMESSAGE("EditorFrame[%p]::resizeView(%p, %p) size=(%d, %d)",
                this, plugView, rect, width, height);
        
        if (m_plugView->canResize() == kResultOk)
            m_widget->setSize(width, height, false, false);

#else
        const QSize size(rect->right  - rect->left, rect->bottom - rect->top);
        DMESSAGE("EditorFrame[%p]::resizeView(%p, %p) size=(%d, %d)",
                this, plugView, rect, size.width(), size.height());

        if (m_plugView->canResize() == kResultOk)
            m_widget->resize(size);
        else
            m_widget->setFixedSize(size);
#endif
        m_resizing = false;

        ViewRect rect0;
        if (m_plugView->getSize(&rect0) != kResultOk)
            return kInternalError;
#if 1
        int width0 = rect0.right  - rect0.left;
        int height0 = rect0.bottom - rect0.top;
        
        if ( (width0 != width) && (height0 != height) )
            m_plugView->onSize(&rect0);
#else
        const QSize size0(
                rect0.right  - rect0.left,
                rect0.bottom - rect0.top);
        if (size != size0)
                m_plugView->onSize(&rect0);
#endif
        return kResultOk;
    }

    tresult PLUGIN_API queryInterface (const TUID _iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
                FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid))
        {
            addRef();
            *obj = this;
            return kResultOk;
        }

        return m_runLoop->queryInterface(_iid, obj);
    }

    uint32 PLUGIN_API addRef  () override { return 1002; }
    uint32 PLUGIN_API release () override { return 1002; }

private:

    // Instance members.
    IPlugView *m_plugView;
    X11PluginUI *m_widget;
    IPtr<RunLoop> m_runLoop;
    bool m_resizing;
};


VST3_Plugin::VST3_Plugin() :
    Plugin_Module(),
    m_module(nullptr),
    m_handler(nullptr),
    m_component(nullptr),
    m_controller(nullptr),
    m_unitInfos(nullptr),
    m_processor(nullptr),
    m_processing(false),
    _plugin_filename(),
    m_sName(),
    m_iAudioIns(0),
    m_iAudioOuts(0),
    m_iMidiIns(0),
    m_iMidiOuts(0),
    _audio_in_buffers(nullptr),
    _audio_out_buffers(nullptr),
    _activated(false),
    m_bRealtime(false),
    m_bConfigure(false),
    m_bEditor(false),
    _position(0),
    _bpm(120.0f),
    _rolling(false),
    _bEditorCreated(false),
    _x_is_resizable(false),
    _x_is_visible(false),
    _X11_UI(nullptr),
    m_plugView(nullptr)
{
    _plug_type = VST3;

    log_create();
}

VST3_Plugin::~VST3_Plugin()
{
    log_destroy();

    deactivate();

    m_processor = nullptr;
    m_handler = nullptr;

    if ( _audio_in_buffers )
    {
        delete []_audio_in_buffers;
        _audio_in_buffers = nullptr;
    }

    if ( _audio_out_buffers )
    {
        delete []_audio_out_buffers;
        _audio_out_buffers = nullptr;
    }

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].disconnect();
            midi_input[i].jack_port()->shutdown();
            delete midi_input[i].jack_port();
        }
    } 
    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].disconnect();
            midi_output[i].jack_port()->shutdown();
            delete midi_output[i].jack_port();
        }
    }

    midi_output.clear();
    midi_input.clear();
}

bool
VST3_Plugin::load_plugin ( Module::Picked picked )
{
    _plugin_filename = picked.s_unique_id;

    if ( ! std::filesystem::exists(_plugin_filename) )
    {
        // FIXME check different location
        DMESSAGE("Failed to find a suitable VST3 bundle binary %s", _plugin_filename.c_str());
        return false;
    }

    if ( !open_descriptor(0) )
    {
        DMESSAGE("Could not open descriptor %s", _plugin_filename.c_str());
        return false;
    }

    base_label(m_sName.c_str());

    initialize_plugin();

    // Properties...
//    m_sName = m_pImpl->name();
//    m_sLabel = m_sName.simplified().replace(QRegularExpression("[\\s|\\.|\\-]+"), "_");
//    m_iUniqueID = m_pImpl->uniqueID();

    m_iAudioIns  = numChannels(Vst::kAudio, Vst::kInput);
    m_iAudioOuts = numChannels(Vst::kAudio, Vst::kOutput);
    m_iMidiIns   = numChannels(Vst::kEvent, Vst::kInput);
    m_iMidiOuts  = numChannels(Vst::kEvent, Vst::kOutput);

    Vst::IEditController *controller = m_controller;
    if (controller)
    {
        IPtr<IPlugView> editor =
                owned(controller->createView(Vst::ViewType::kEditor));
        m_bEditor = (editor != nullptr);
    }

    // FIXME
    m_bRealtime  = true;
    m_bConfigure = true;

    create_audio_ports();
    create_midi_ports();
    create_control_ports();

    if ( !process_reset() )
    {
        DMESSAGE("Process reset failed!");
        return false;
    }

    if(!_plugin_ins)
        is_zero_input_synth(true);

    return true;
}

bool
VST3_Plugin::configure_inputs ( int n )
{
    /* The synth case - no inputs and JACK module has one */
    if( ninputs() == 0 && n == 1)
    {
        _crosswire = false;
    }
    else if ( ninputs() != n )
    {
        _crosswire = false;

        if ( 1 == n && plugin_ins() > 1 )
        {
            DMESSAGE( "Cross-wiring plugin inputs" );
            _crosswire = true;

            audio_input.clear();

            for ( int i = n; i--; )
                audio_input.push_back( Port( this, Port::INPUT, Port::AUDIO ) );
        }

        else if ( n == plugin_ins() )
        {
            DMESSAGE( "Plugin input configuration is a perfect match" );
        }
        else
        {
            DMESSAGE( "Unsupported input configuration" );
            return false;
        }
    }

    return true;

}

void
VST3_Plugin::handle_port_connection_change ( void )
{
    if ( loaded() )
    {
        if ( _crosswire )
        {
            for ( int i = 0; i < plugin_ins(); ++i )
                set_input_buffer( i, audio_input[0].buffer() );
        }
        else
        {
            for ( unsigned int i = 0; i < audio_input.size(); ++i )
                set_input_buffer( i, audio_input[i].buffer() );
        }

        for ( unsigned int i = 0; i < audio_output.size(); ++i )
            set_output_buffer( i, audio_output[i].buffer() );
    }
}

void
VST3_Plugin::handle_chain_name_changed ( void )
{
    Module::handle_chain_name_changed();

    if ( ! chain()->strip()->group()->single() )
    {
        for ( unsigned int i = 0; i < midi_input.size(); i++ )
        {
            if(!(midi_input[i].type() == Port::MIDI))
                continue;

            if(midi_input[i].jack_port())
            {
                midi_input[i].jack_port()->trackname( chain()->name() );
                midi_input[i].jack_port()->rename();
            }
        }
        for ( unsigned int i = 0; i < midi_output.size(); i++ )
        {
            if(!(midi_output[i].type() == Port::MIDI))
                continue;

            if(midi_output[i].jack_port())
            {
                midi_output[i].jack_port()->trackname( chain()->name() );
                midi_output[i].jack_port()->rename();
            }
        }
    }
}

void
VST3_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    
}

void
VST3_Plugin::resize_buffers ( nframes_t buffer_size )
{
    
}

void
VST3_Plugin::bypass ( bool v )
{
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

void
VST3_Plugin::freeze_ports ( void )
{
    Module::freeze_ports();

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].jack_port()->freeze();
            midi_input[i].jack_port()->shutdown();
        }
    }

    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].jack_port()->freeze();
            midi_output[i].jack_port()->shutdown();
        }
    } 
}

void
VST3_Plugin::thaw_ports ( void )
{
    Module::thaw_ports();

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {   
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].jack_port()->client( chain()->client() );
            midi_input[i].jack_port()->trackname( trackname );
            midi_input[i].jack_port()->thaw();
        }
    }

    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].jack_port()->client( chain()->client() );
            midi_output[i].jack_port()->trackname( trackname );
            midi_output[i].jack_port()->thaw();
        }
    }
}

void
VST3_Plugin::configure_midi_inputs ()
{
    if(!midi_input.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += midi_input[i].name();

        DMESSAGE("CONFIGURE MIDI INPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Input, JACK::Port::MIDI );
        midi_input[i].jack_port(jack_port);

        if( !midi_input[i].jack_port()->activate() )
        {
            delete midi_input[i].jack_port();
            midi_input[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI IN port" );
            return;
        }
    }
}

void
VST3_Plugin::configure_midi_outputs ()
{
    if(!midi_output.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += midi_output[i].name();

        DMESSAGE("CONFIGURE MIDI OUTPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Output, JACK::Port::MIDI );
        midi_output[i].jack_port(jack_port);

        if( !midi_output[i].jack_port()->activate() )
        {
            delete midi_output[i].jack_port();
            midi_output[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI OUT port" );
            return;
        }
    }
}

nframes_t
VST3_Plugin::get_module_latency ( void ) const
{
    return 0;
}

void
VST3_Plugin::process ( nframes_t nframes )
{
    handle_port_connection_change();

    if ( unlikely( bypass() ) )
    {
        /* If this is a mono to stereo plugin, then duplicate the input channel... */
        /* There's not much we can do to automatically support other configurations. */
        if ( ninputs() == 1 && noutputs() == 2 )
        {
            buffer_copy( static_cast<sample_t*>( audio_output[1].buffer() ),
                    static_cast<sample_t*>( audio_input[0].buffer() ), nframes );
        }

        _latency = 0;
    }
    else
    {
        if (!m_processor)
            return;

        if (!m_processing)
            return;

        process_jack_transport( nframes );

        for( unsigned int i = 0; i < midi_input.size(); ++i )
        {
            /* JACK MIDI in to plugin MIDI in */
            process_jack_midi_in( nframes, i );
        }

        for ( unsigned int i = 0; i < midi_output.size(); ++i)
        {
            /* Plugin to JACK MIDI out */
            process_jack_midi_out( nframes, i);
        }

        m_events_out.clear();

//        m_buffers_in.channelBuffers32 = ins;
//        m_buffers_out.channelBuffers32 = outs;
        m_process_data.numSamples = nframes;

        if (m_processor->process(m_process_data) != kResultOk)
        {
            WARNING("[%p]::process() FAILED!", this);
        }

        m_events_in.clear();
        m_params_in.clear();
    }
}

// Set/add a parameter value/point.
void
VST3_Plugin::setParameter (
	Vst::ParamID id, Vst::ParamValue value, uint32 offset )
{
    int32 index = 0;
    Vst::IParamValueQueue *queue = m_params_in.addParameterData(id, index);
    if (queue && (queue->addPoint(offset, value, index) != kResultOk))
    {
        WARNING("setParameter(%u, %g, %u) FAILED!", this, id, value, offset);
    }
}

void
VST3_Plugin::set_control_value(unsigned long port_index, float value, bool update_custom_ui)
{
    if( port_index >= control_input.size())
    {
        WARNING("Invalid Port Index = %d: Value = %f", port_index, value);
        return;
    }

    _is_from_custom_ui = !update_custom_ui;

    control_input[port_index].control_value(value);

    if (!dirty())
        set_dirty();
}

/**
 From Host to plugin - set parameter values.
 */
void
VST3_Plugin::updateParam(Vst::ParamID id, float fValue)
{
    Vst::IEditController *controller = m_controller;
    if (!controller)
        return;

    DMESSAGE("UpdateParam ID = %u: Value = %f", id, fValue);

    const Vst::ParamValue value = Vst::ParamValue(fValue);

    setParameter(id, value, 0); // sends to plugin
    controller->setParamNormalized(id, value);  // For gui ???
}

// Parameters update methods.
void
VST3_Plugin::updateParamValues(bool update_custom_ui)
{
    for ( unsigned int i = 0; i < control_input.size(); ++i)
    {
        float value = (float) getParameter(control_input[i].hints.parameter_id);

        if( control_input[i].control_value() != value)
        {
            set_control_value(i, value, update_custom_ui);
        }
    }
}

// Get current parameter value.
Vst::ParamValue
VST3_Plugin::getParameter ( Vst::ParamID id ) const
{
    Vst::IEditController *controller = m_controller;
    if (controller)
        return controller->getParamNormalized(id);
    else
        return 0.0;
}

tresult
VST3_Plugin::notify ( Vst::IMessage *message )
{
    DMESSAGE("[%p]::notify(%p)", this, message);

    Vst::IComponent *component = m_component;
    FUnknownPtr<Vst::IConnectionPoint> component_cp(component);
    if (component_cp)
        component_cp->notify(message);

    Vst::IEditController *controller = m_controller;
    FUnknownPtr<Vst::IConnectionPoint> controller_cp(controller);
    if (controller_cp)
        controller_cp->notify(message);

    return kResultOk;
}

bool
VST3_Plugin::try_custom_ui()
{
    /* Toggle show and hide */
    if(_bEditorCreated)
    {
        if (_x_is_visible)
        {
            hide_custom_ui();
            return true;
        }
        else
        {
            show_custom_ui();
            return true;
        }
    }
    
#if 0
    // Is it already there?
    if (m_pEditorWidget)
    {
        if (!m_pEditorWidget->isVisible())
        {
                moveWidgetPos(m_pEditorWidget, editorPos());
                m_pEditorWidget->show();
        }
        m_pEditorWidget->raise();
        m_pEditorWidget->activateWindow();
        return;
    }
#endif

    if (!openEditor())
        return false;

    IPlugView *plugView = m_plugView;
    if (!plugView)
        return false;

    if (plugView->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultOk)
    {
        DMESSAGE("[%p]::openEditor"
                " *** X11 Window platform is not supported (%s).", this,
                kPlatformTypeX11EmbedWindowID);
        return false;
    }
    
    if (m_plugView->canResize() == kResultOk)
        _x_is_resizable = true;
    
    _X11_UI = new X11PluginUI(this, _x_is_resizable, false);
    _X11_UI->setTitle(label());
    
    m_pEditorFrame = new EditorFrame(plugView, _X11_UI);
    
    void *wid = _X11_UI->getPtr();
    
    if (plugView->attached(wid, kPlatformTypeX11EmbedWindowID) != kResultOk)
    {
        DMESSAGE(" *** Failed to create/attach editor window - %s.", label());
        closeEditor();
        return false;
    }
#if 0
    m_pEditorWidget = new EditorWidget(pParent, wflags);
    m_pEditorWidget->setAttribute(Qt::WA_QuitOnClose, false);
    m_pEditorWidget->setWindowTitle(pType->name());
    m_pEditorWidget->setWindowIcon(QIcon(":/images/qtractorPlugin.svg"));
    m_pEditorWidget->setPlugin(this);

    m_pEditorFrame = new EditorFrame(plugView, m_pEditorWidget);

    void *wid = (void *) m_pEditorWidget->parentWinId();


    if (plugView->attached(wid, kPlatformTypeX11EmbedWindowID) != kResultOk)
    {
#ifdef CONFIG_DEBUG
        qDebug("qtractorVst3Plugin::[%p]::openEditor(%p)"
                " *** Failed to create/attach editor window.", this, pParent);
#endif
        closeEditor();
        return false;
    }

    // Final stabilization...
    updateEditorTitle();
    moveWidgetPos(m_pEditorWidget, editorPos());
    setEditorVisible(true);

#endif
    _bEditorCreated = show_custom_ui();

    return _bEditorCreated;
}

// Editor controller methods.
bool
VST3_Plugin::openEditor (void)
{
    closeEditor();

#ifdef CONFIG_VST3_XCB
    g_hostContext.openXcbConnection();
#endif

//    g_hostContext.startTimer(200);

    Vst::IEditController *controller = m_controller;
    if (controller)
        m_plugView = owned(controller->createView(Vst::ViewType::kEditor));

    return (m_plugView != nullptr);
}

void
VST3_Plugin::closeEditor (void)
{
    m_plugView = nullptr;

//    g_hostContext.stopTimer();

#ifdef CONFIG_VST3_XCB
    g_hostContext.closeXcbConnection();
#endif
}

bool
VST3_Plugin::show_custom_ui()
{
#if 0
    if (_is_floating)
    {
        _x_is_visible = _gui->show(_plugin);
        Fl::add_timeout( 0.03f, &CLAP_Plugin::custom_update_ui, this );
        return _x_is_visible;
    }
#endif
    _X11_UI->show();
    _X11_UI->focus();

    _x_is_visible = true;

    Fl::add_timeout( 0.03f, &VST3_Plugin::custom_update_ui, this );

    return true;
}

/**
 Callback for custom ui idle interface
 */
void 
VST3_Plugin::custom_update_ui ( void *v )
{
    ((VST3_Plugin*)v)->custom_update_ui_x();
}

/**
 The idle callback to update_custom_ui()
 */
void
VST3_Plugin::custom_update_ui_x()
{
#if 0
    if (!_is_floating)
    {
        if(_x_is_visible)
            _X11_UI->idle();
    }
#endif
    _X11_UI->idle();
#if 0
    for (LinkedList<HostTimerDetails>::Itenerator it = _fTimers.begin2(); it.valid(); it.next())
    {
        const uint32_t currentTimeInMs = water::Time::getMillisecondCounter();
        HostTimerDetails& timer(it.getValue(kTimerFallbackNC));

        if (currentTimeInMs > timer.lastCallTimeInMs + timer.periodInMs)
        {
            timer.lastCallTimeInMs = currentTimeInMs;
            _timer_support->on_timer(_plugin, timer.clapId);
        }
    }
#endif
    if(_x_is_visible)
    {
        Fl::repeat_timeout( 0.03f, &VST3_Plugin::custom_update_ui, this );
    }
    else
    {
        hide_custom_ui();
    }
}

bool
VST3_Plugin::hide_custom_ui()
{
    DMESSAGE("Closing Custom Interface");
    closeEditor();
#if 0
    if (_is_floating)
    {
        _x_is_visible = false;
        Fl::remove_timeout(&CLAP_Plugin::custom_update_ui, this);
        return _gui->hide(_plugin);
    }
#endif
    Fl::remove_timeout(&VST3_Plugin::custom_update_ui, this);

    _x_is_visible = false;

    if (_X11_UI != nullptr)
        _X11_UI->hide();

    if (_bEditorCreated)
    {
      //  _gui->destroy(_plugin);
        _bEditorCreated = false;
    }

    if(_X11_UI != nullptr)
    {
        delete _X11_UI;
        _X11_UI = nullptr;
    }

    return true;
}


void
VST3_Plugin::handlePluginUIClosed()
{
    _x_is_visible = false;
}

void
VST3_Plugin::handlePluginUIResized(const uint width, const uint height)
{

}

// Parameter finder (by id).
unsigned long 
VST3_Plugin::findParamId ( int id ) const
{
    std::unordered_map<int, unsigned long>::const_iterator got
        = m_paramIds.find (int(id));

     if ( got == m_paramIds.end() )
     {
         // probably a control out - we don't do anything with these
         // DMESSAGE("Param Id not found = %d", param_id);
         return 0;
     }

    unsigned long index = got->second;
    return index;
}


// File loader.
bool
VST3_Plugin::open_file ( const std::string& sFilename )
{
    DMESSAGE("Open %s", sFilename.c_str());

    m_module = ::dlopen(sFilename.c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!m_module)
        return false;

    typedef bool (*VST3_ModuleEntry)(void *);
    const VST3_ModuleEntry module_entry
            = VST3_ModuleEntry(::dlsym(m_module, "ModuleEntry"));

    if (module_entry)
        module_entry(m_module);

    return true;
}

bool
VST3_Plugin::open_descriptor(unsigned long iIndex)
{
    close_descriptor();
    
    if (!open_file(_plugin_filename))
    {
        DMESSAGE("Could not open %s", _plugin_filename.c_str());
        return false;
    }
    #if 0
    typedef bool (PLUGIN_API *VST3_ModuleEntry)(void *);
    const VST3_ModuleEntry module_entry
            = reinterpret_cast<VST3_ModuleEntry> (m_pFile->resolve("ModuleEntry"));
    if (module_entry)
            module_entry(m_pFile->module());
    #endif
    typedef IPluginFactory *(PLUGIN_API *VST3_GetPluginFactory)();
    const VST3_GetPluginFactory get_plugin_factory
            = reinterpret_cast<VST3_GetPluginFactory> (::dlsym(m_module, "GetPluginFactory"));
    //	= reinterpret_cast<VST3_GetPluginFactory> (m_pFile->resolve("GetPluginFactory"));
    if (!get_plugin_factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to resolve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory *factory = get_plugin_factory();
    if (!factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    PFactoryInfo factoryInfo;
    if (factory->getFactoryInfo(&factoryInfo) != kResultOk)
    {
        DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory information.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory2 *factory2 = FUnknownPtr<IPluginFactory2> (factory);
    IPluginFactory3 *factory3 = FUnknownPtr<IPluginFactory3> (factory);

    if (factory3)
    {
        factory3->setHostContext(g_hostContext.get());
    }

    const int32 nclasses = factory->countClasses();

    unsigned long i = 0;

    for (int32 n = 0; n < nclasses; ++n)
    {
        PClassInfo classInfo;
        if (factory->getClassInfo(n, &classInfo) != kResultOk)
            continue;

        if (::strcmp(classInfo.category, kVstAudioEffectClass))
            continue;

        if (iIndex == i)
        {
#if 1
            PClassInfoW classInfoW;
            if (factory3 && factory3->getClassInfoUnicode(n, &classInfoW) == kResultOk)
            {
                    m_sName = utf16_to_utf8(classInfoW.name);
                 //   m_sName = fromTChar(classInfoW.name);
                 //   m_sCategory = QString::fromLocal8Bit(classInfoW.category);
                 //   m_sSubCategories = QString::fromLocal8Bit(classInfoW.subCategories);
                 //   m_sVendor = fromTChar(classInfoW.vendor);
                 //   m_sVersion = fromTChar(classInfoW.version);
                 //   m_sSdkVersion = fromTChar(classInfoW.sdkVersion);
            } else {
                    PClassInfo2 classInfo2;
                    if (factory2 && factory2->getClassInfo2(n, &classInfo2) == kResultOk)
                    {
                        m_sName = classInfo2.name;
                         //   m_sName = QString::fromLocal8Bit(classInfo2.name);
                         //   m_sCategory = QString::fromLocal8Bit(classInfo2.category);
                         //   m_sSubCategories = QString::fromLocal8Bit(classInfo2.subCategories);
                         //   m_sVendor = QString::fromLocal8Bit(classInfo2.vendor);
                         //   m_sVersion = QString::fromLocal8Bit(classInfo2.version);
                         //   m_sSdkVersion = QString::fromLocal8Bit(classInfo2.sdkVersion);
                    } else 
                    {
                        m_sName = classInfo.name;
                         //   m_sName = QString::fromLocal8Bit(classInfo.name);
                         //   m_sCategory = QString::fromLocal8Bit(classInfo.category);
                         //   m_sSubCategories.clear();
                         //   m_sVendor.clear();
                         //   m_sVersion.clear();
                         //   m_sSdkVersion.clear();
                    }
            }
#if 0
            if (m_sVendor.isEmpty())
                    m_sVendor = QString::fromLocal8Bit(factoryInfo.vendor);
            if (m_sEmail.isEmpty())
                    m_sEmail = QString::fromLocal8Bit(factoryInfo.email);
            if (m_sUrl.isEmpty())
                    m_sUrl = QString::fromLocal8Bit(factoryInfo.url);

            m_iUniqueID = qHash(QByteArray(classInfo.cid, sizeof(TUID)));
#endif
#endif  // 1
            Vst::IComponent *component = nullptr;
            if (factory->createInstance(
                    classInfo.cid, Vst::IComponent::iid,
                    (void **) &component) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to create plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                return false;
            }

            m_component = owned(component);

            if (m_component->initialize(g_hostContext.get()) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to initialize plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                close();
                return false;
            }

            Vst::IEditController *controller = nullptr;
            if (m_component->queryInterface(
                    Vst::IEditController::iid,
                    (void **) &controller) != kResultOk)
            {
                TUID controller_cid;
                if (m_component->getControllerClassId(controller_cid) == kResultOk)
                {
                    if (factory->createInstance(
                            controller_cid, Vst::IEditController::iid,
                            (void **) &controller) != kResultOk)
                    {
                        DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
                                " *** Failed to create plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                    }

                    if (controller &&
                            controller->initialize(g_hostContext.get()) != kResultOk)
                    {
                        DMESSAGE("[%p]::open(\"%s\", %lu)"
                                " *** Failed to initialize plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                        controller = nullptr;
                    }
                }
            }

            if (controller) m_controller = owned(controller);

            Vst::IUnitInfo *unitInfos = nullptr;
            if (m_component->queryInterface(
                            Vst::IUnitInfo::iid,
                            (void **) &unitInfos) != kResultOk)
            {
                if (m_controller &&
                        m_controller->queryInterface(
                                Vst::IUnitInfo::iid,
                                (void **) &unitInfos) != kResultOk)
                {
                    DMESSAGE("[%p]::open(\"%s\", %lu)"
                            " *** Failed to create plug-in units information.", this,
                            _plugin_filename.c_str(), iIndex);
                }
            }

            if (unitInfos) m_unitInfos = owned(unitInfos);

            // Connect components...
            if (m_component && m_controller)
            {
                FUnknownPtr<Vst::IConnectionPoint> component_cp(m_component);
                FUnknownPtr<Vst::IConnectionPoint> controller_cp(m_controller);
                if (component_cp && controller_cp)
                {
                        component_cp->connect(controller_cp);
                        controller_cp->connect(component_cp);
                }
            }

            return true;
        }

        ++i;
    }

    return false;
}

void
VST3_Plugin::close_descriptor()
{
    if (m_component && m_controller)
    {
        FUnknownPtr<Vst::IConnectionPoint> component_cp(m_component);
        FUnknownPtr<Vst::IConnectionPoint> controller_cp(m_controller);
        if (component_cp && controller_cp)
        {
            component_cp->disconnect(controller_cp);
            controller_cp->disconnect(component_cp);
        }
    }

    m_unitInfos = nullptr;

    if (m_component && m_controller &&
            FUnknownPtr<Vst::IEditController> (m_component).getInterface())
    {
        m_controller->terminate();
    }

    m_controller = nullptr;

    if (m_component)
    {
        m_component->terminate();
        m_component = nullptr;
        typedef bool (PLUGIN_API *VST3_ModuleExit)();
        const VST3_ModuleExit module_exit
                = reinterpret_cast<VST3_ModuleExit> (::dlsym(m_module, "ModuleExit"));
              //  = reinterpret_cast<VST3_ModuleExit> (m_pFile->resolve("ModuleExit"));

        if (module_exit)
            module_exit();
    }
}

void
VST3_Plugin::set_input_buffer ( int n, void *buf )
{
    _audio_in_buffers[n] = static_cast<float*>( buf );
    m_buffers_in.channelBuffers32 = _audio_in_buffers;
}

void
VST3_Plugin::set_output_buffer ( int n, void *buf )
{
    _audio_out_buffers[n] = static_cast<float*>( buf );
    m_buffers_out.channelBuffers32 = _audio_out_buffers;
}

bool
VST3_Plugin::loaded ( void ) const
{
    if ( m_module )
        return true;

    return false;
}

bool
VST3_Plugin::process_reset()
{
    if (!m_processor)
        return false;

    deactivate();
    
    _position = 0;
    _bpm = 120.0f;
    _rolling = false;

    // Initialize running state...
    m_params_in.clear();

    m_events_in.clear();
    m_events_out.clear();

    Vst::ProcessSetup setup;
//    const bool bFreewheel    = pAudioEngine->isFreewheel();
//    setup.processMode        = (bFreewheel ? Vst::kOffline :Vst::kRealtime);
    setup.processMode        = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = buffer_size();      // FIXME Check
    setup.sampleRate         = float(sample_rate());
//    setup.maxSamplesPerBlock = pAudioEngine->bufferSizeEx();
//    setup.sampleRate         = float(pAudioEngine->sampleRate());

    if (m_processor->setupProcessing(setup) != kResultOk)
        return false;

    // Setup processor audio I/O buffers...
    m_buffers_in.silenceFlags      = 0;
    m_buffers_in.numChannels       = m_iAudioIns;
    m_buffers_in.channelBuffers32  = nullptr;

    m_buffers_out.silenceFlags     = 0;
    m_buffers_out.numChannels      = m_iAudioOuts;
    m_buffers_out.channelBuffers32 = nullptr;

    // Setup processor data struct...
    m_process_data.numSamples             = buffer_size();
    //m_process_data.numSamples             = pAudioEngine->blockSize();
    m_process_data.symbolicSampleSize     = Vst::kSample32;

    if (m_iAudioIns > 0) {
            m_process_data.numInputs          = 1;
            m_process_data.inputs             = &m_buffers_in;
    } else {
            m_process_data.numInputs          = 0;
            m_process_data.inputs             = nullptr;
    }

    if (m_iAudioOuts > 0) {
            m_process_data.numOutputs         = 1;
            m_process_data.outputs            = &m_buffers_out;
    } else {
            m_process_data.numOutputs         = 0;
            m_process_data.outputs            = nullptr;
    }

    m_process_data.processContext         = g_hostContext.processContext();
    m_process_data.inputEvents            = &m_events_in;
    m_process_data.outputEvents           = &m_events_out;
    m_process_data.inputParameterChanges  = &m_params_in;
    m_process_data.outputParameterChanges = nullptr; //&m_params_out;

    activate();

    return true;
}

void
VST3_Plugin::process_jack_transport ( uint32_t nframes )
{
    // Get Jack transport position
    jack_position_t pos;
    const bool rolling =
        (chain()->client()->transport_query(&pos) == JackTransportRolling);

    // If transport state is not as expected, then something has changed
    const bool has_bbt = (pos.valid & JackPositionBBT);
    const bool xport_changed =
      (rolling != _rolling || pos.frame != _position ||
       (has_bbt && pos.beats_per_minute != _bpm));
    
    g_hostContext.updateProcessContext(pos, xport_changed, has_bbt);

    // Update transport state to expected values for next cycle
    _position = rolling ? pos.frame + nframes : pos.frame;
    _bpm      = has_bbt ? pos.beats_per_minute : _bpm;
    _rolling  = rolling;
}

void
VST3_Plugin::process_jack_midi_in ( uint32_t nframes, unsigned int port )
{
    /* Process any MIDI events from jack */
    if ( midi_input[port].jack_port() )
    {
        void *buf = midi_input[port].jack_port()->buffer( nframes );

        for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i)
        {
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, i);

            process_midi_in(ev.buffer, ev.size, ev.time, 0);
        }
    }
}

void
VST3_Plugin::process_midi_in (
	unsigned char *data, unsigned int size,
	unsigned long offset, unsigned short port )
{
    for (uint32_t i = 0; i < size; ++i)
    {
        // channel status
        const int channel = (data[i] & 0x0f);// + 1;
        const int status  = (data[i] & 0xf0);

        // all system common/real-time ignored
        if (status == 0xf0)
                continue;

        // check data size (#1)
        if (++i >= size)
                break;

        // channel key
        const int key = (data[i] & 0x7f);

        // program change
        if (status == 0xc0) {
                // TODO: program-change...
                continue;
        }

        // after-touch
        if (status == 0xd0)
        {
            const MidiMapKey mkey(port, channel, Vst::kAfterTouch);
            //const Vst::ParamID id = m_midiMap.value(mkey, Vst::kNoParamId);
            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = m_midiMap.find (mkey);

            if (got == m_midiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float pre = float(key) / 127.0f;
                setParameter(id, Vst::ParamValue(pre), offset);
            }
            continue;
        }

        // check data size (#2)
        if (++i >= size)
                break;

        // channel value (normalized)
        const int value = (data[i] & 0x7f);

        Vst::Event event;
        ::memset(&event, 0, sizeof(Vst::Event));
        event.busIndex = port;
        event.sampleOffset = offset;
        event.flags = Vst::Event::kIsLive;

        // note on
        if (status == 0x90)
        {
            event.type = Vst::Event::kNoteOnEvent;
            event.noteOn.noteId = -1;
            event.noteOn.channel = channel;
            event.noteOn.pitch = key;
            event.noteOn.velocity = float(value) / 127.0f;
            m_events_in.addEvent(event);
        }
        // note off
        else if (status == 0x80)
        {
            event.type = Vst::Event::kNoteOffEvent;
            event.noteOff.noteId = -1;
            event.noteOff.channel = channel;
            event.noteOff.pitch = key;
            event.noteOff.velocity = float(value) / 127.0f;
            m_events_in.addEvent(event);
        }
        // key pressure/poly.aftertouch
        else if (status == 0xa0)
        {
            event.type = Vst::Event::kPolyPressureEvent;
            event.polyPressure.channel = channel;
            event.polyPressure.pitch = key;
            event.polyPressure.pressure = float(value) / 127.0f;
            m_events_in.addEvent(event);
        }
        // control-change
        else if (status == 0xb0)
        {
            const MidiMapKey mkey(port, channel, key);
            //  const Vst::ParamID id = m_midiMap.value(mkey, Vst::kNoParamId);
            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = m_midiMap.find (mkey);

            if (got == m_midiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float val = float(value) / 127.0f;
                setParameter(id, Vst::ParamValue(val), offset);
            }
        }
        // pitch-bend
        else if (status == 0xe0)
        {
            const MidiMapKey mkey(port, channel, Vst::kPitchBend);
            //  const Vst::ParamID id = m_midiMap.value(mkey, Vst::kNoParamId);
            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = m_midiMap.find (mkey);

            if (got == m_midiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float pitchbend
                        = float(key + (value << 7)) / float(0x3fff);
                setParameter(id, Vst::ParamValue(pitchbend), offset);
            }
        }
    }
}

void
VST3_Plugin::process_jack_midi_out ( uint32_t nframes, unsigned int port )
{
    void* buf = NULL;

    if ( midi_output[port].jack_port() )
    {
        buf = midi_output[port].jack_port()->buffer( nframes );
        jack_midi_clear_buffer(buf);

        // Process MIDI output stream, if any...
        VST3IMPL::EventList& events_out = m_events_out;
        const int32 nevents = events_out.getEventCount();
        for (int32 i = 0; i < nevents; ++i)
        {
            Vst::Event event;
            if (events_out.getEvent(i, event) == kResultOk)
            {
                switch (event.type)
                {
                case Vst::Event::kNoteOnEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_NOTE_ON + event.noteOn.channel;
                    midi_note[1] = event.noteOn.pitch;
                    midi_note[2] = (unsigned char)(event.noteOn.velocity * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI note on error = %d", ret);

                    break;
                }
                case Vst::Event::kNoteOffEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_NOTE_OFF + event.noteOff.channel;
                    midi_note[1] = event.noteOff.pitch;
                    midi_note[2] = (unsigned char)(event.noteOff.velocity * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI note off error = %d", ret);

                    break;
                }
                case Vst::Event::kPolyPressureEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_CHANNEL_PRESSURE + event.polyPressure.channel;
                    midi_note[1] = event.polyPressure.pitch;
                    midi_note[2] = (unsigned char)(event.polyPressure.pressure * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI polyPressure error = %d", ret);

                    break;
                }
                }
            }
        }
    }
}


void
VST3_Plugin::initialize_plugin()
{
    clear_plugin();

    Vst::IComponent *component = m_component;
    if (!component)
            return;

    Vst::IEditController *controller = m_controller;
    if (controller)
    {
        m_handler = owned(NEW VST3_Plugin::Handler(this));
        controller->setComponentHandler(m_handler);
    }

    m_processor = FUnknownPtr<Vst::IAudioProcessor> (component);
#if 0
    if (controller)
    {
            const int32 nparams = controller->getParameterCount();
            for (int32 i = 0; i < nparams; ++i) {
                    Vst::ParameterInfo paramInfo;
                    ::memset(&paramInfo, 0, sizeof(Vst::ParameterInfo));
                    if (controller->getParameterInfo(i, paramInfo) == kResultOk) {
                            if (m_programParamInfo.unitId != Vst::UnitID(-1))
                                    continue;
                            if (paramInfo.flags & Vst::ParameterInfo::kIsProgramChange)
                                    m_programParamInfo = paramInfo;
                    }
            }
            if (m_programParamInfo.unitId != Vst::UnitID(-1)) {
                    Vst::IUnitInfo *unitInfos = pType->impl()->unitInfos();
                    if (unitInfos) {
                            const int32 nunits = unitInfos->getUnitCount();
                            for (int32 i = 0; i < nunits; ++i) {
                                    Vst::UnitInfo unitInfo;
                                    if (unitInfos->getUnitInfo(i, unitInfo) != kResultOk)
                                            continue;
                                    if (unitInfo.id != m_programParamInfo.unitId)
                                            continue;
                                    const int32 nlists = unitInfos->getProgramListCount();
                                    for (int32 j = 0; j < nlists; ++j) {
                                            Vst::ProgramListInfo programListInfo;
                                            if (unitInfos->getProgramListInfo(j, programListInfo) != kResultOk)
                                                    continue;
                                            if (programListInfo.id != unitInfo.programListId)
                                                    continue;
                                            const int32 nprograms = programListInfo.programCount;
                                            for (int32 k = 0; k < nprograms; ++k) {
                                                    Vst::String128 name;
                                                    if (unitInfos->getProgramName(
                                                                    programListInfo.id, k, name) == kResultOk)
                                                            m_programs.append(fromTChar(name));
                                            }
                                            break;
                                    }
                            }
                    }
            }
            if (m_programs.isEmpty() && m_programParamInfo.stepCount > 0) {
                    const int32 nprograms = m_programParamInfo.stepCount + 1;
                    for (int32 k = 0; k < nprograms; ++k) {
                            const Vst::ParamValue value
                                    = Vst::ParamValue(k)
                                    / Vst::ParamValue(m_programParamInfo.stepCount);
                            Vst::String128 name;
                            if (controller->getParamStringByValue(
                                            m_programParamInfo.id, value, name) == kResultOk)
                                    m_programs.append(fromTChar(name));
                    }
            }
    }
#endif
    if (controller)
    {
        const int32 nports = m_iMidiIns;
        FUnknownPtr<Vst::IMidiMapping> midiMapping(controller);
        if (midiMapping && nports > 0)
        {
            for (int16 i = 0; i < Vst::kCountCtrlNumber; ++i)
            { // controllers...
                for (int32 j = 0; j < nports; ++j)
                { // ports...
                    for (int16 k = 0; k < 16; ++k)
                    { // channels...
                        Vst::ParamID id = Vst::kNoParamId;
                        if (midiMapping->getMidiControllerAssignment(
                                        j, k, Vst::CtrlNumber(i), id) == kResultOk)
                        {
                            std::pair<MidiMapKey, Vst::ParamID> prm ( MidiMapKey(j, k, i), id );
                            m_midiMap.insert(prm);
                        }
                    }
                }
            }
        }
    }
}

void
VST3_Plugin::clear_plugin()
{
#if 0
    ::memset(&m_programParamInfo, 0, sizeof(Vst::ParameterInfo));
    m_programParamInfo.id = Vst::kNoParamId;
    m_programParamInfo.unitId = Vst::UnitID(-1);
    m_programs.clear();
#endif
    m_midiMap.clear();
}

int
VST3_Plugin::numChannels (
	Vst::MediaType type, Vst::BusDirection direction ) const
{
    if (!m_component)
            return -1;

    int nchannels = 0;

    const int32 nbuses = m_component->getBusCount(type, direction);
    for (int32 i = 0; i < nbuses; ++i)
    {
        Vst::BusInfo busInfo;
        if (m_component->getBusInfo(type, direction, i, busInfo) == kResultOk)
        {
            if ((busInfo.busType == Vst::kMain) ||
                    (busInfo.flags & Vst::BusInfo::kDefaultActive))
            {
                nchannels += busInfo.channelCount;
            }
        }
    }

    return nchannels;
}

void
VST3_Plugin::create_audio_ports()
{
    _plugin_ins = 0;
    _plugin_outs = 0;
    
    for (uint32_t i = 0; i < m_iAudioIns; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::AUDIO, "input" ) );
        audio_input[_plugin_ins].hints.plug_port_index = i;
        _plugin_ins++;
    }

    for (uint32_t i = 0; i < m_iAudioOuts; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::AUDIO, "output" ) );
        audio_output[_plugin_outs].hints.plug_port_index = i;
        _plugin_outs++;
    }

    _audio_in_buffers = new float * [_plugin_ins];
    _audio_out_buffers = new float * [_plugin_outs];

    MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);
}

void
VST3_Plugin::create_midi_ports()
{
    const int32 inbuses = m_component->getBusCount(Vst::kEvent, Vst::kInput);
    const int32 outbuses = m_component->getBusCount(Vst::kEvent, Vst::kOutput);
    
    for (uint32_t i = 0; i < inbuses; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::MIDI, "midi_in" ) );
    }
    
    for (uint32_t i = 0; i < outbuses; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::MIDI, "midi_out" ) );
    }

    MESSAGE( "Plugin has %i MIDI ins and %i MIDI outs", inbuses, outbuses);
}

void
VST3_Plugin::create_control_ports()
{
    unsigned long control_ins = 0;
    unsigned long control_outs = 0;
    
    Vst::IEditController *controller = m_controller;

    if (controller)
    {
        const int32 nparams = controller->getParameterCount();

        for (int32 i = 0; i < nparams; ++i)
        {
            Port::Direction d = Port::INPUT;

            Vst::ParameterInfo paramInfo;
            ::memset(&paramInfo, 0, sizeof(Vst::ParameterInfo));
            if (controller->getParameterInfo(i, paramInfo) == kResultOk)
            {
                if (paramInfo.flags & Vst::ParameterInfo::kIsHidden)
                    continue;

                if ( !(paramInfo.flags & Vst::ParameterInfo::kIsReadOnly) &&
                        !(paramInfo.flags & Vst::ParameterInfo::kCanAutomate) )
                {
                    continue;
                }

                bool have_control_in = false;

                if (paramInfo.flags & Vst::ParameterInfo::kIsReadOnly)
                {
                    d = Port::OUTPUT;
                    ++control_outs;
                }
                else
                if (paramInfo.flags & Vst::ParameterInfo::kCanAutomate)
                {
                    d = Port::INPUT;
                    ++control_ins;
                    have_control_in = true;
                }

                std::string description = utf16_to_utf8(paramInfo.title);
                description += " ";
                description += utf16_to_utf8 (paramInfo.units);

                Port p( this, d, Port::CONTROL, strdup( description.c_str() ) );

                /* Used for OSC path creation unique symbol */
                std::string osc_symbol = strdup( utf16_to_utf8(paramInfo.shortTitle).c_str() );
                osc_symbol.erase(std::remove(osc_symbol.begin(), osc_symbol.end(), ' '), osc_symbol.end());
                osc_symbol += std::to_string( paramInfo.id );

                p.set_symbol(osc_symbol.c_str());

                p.hints.ranged = true;

                if ( paramInfo.stepCount  == 1)
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }
                else if ( paramInfo.stepCount  == 0 )
                {
                    // p.hints.ranged = false;
                    //paramInfo.
                    // continuous ??? WTF
                    p.hints.minimum = (float) 0.0;
                    p.hints.maximum = (float) 1.0;
                }
                else
                {
                    p.hints.minimum = (float) 0.0;
                    p.hints.maximum = (float) paramInfo.stepCount;
                    p.hints.type = Port::Hints::INTEGER;
                }

                p.hints.default_value = (float) paramInfo.defaultNormalizedValue;

                p.hints.parameter_id = paramInfo.id;

                if (paramInfo.flags & Vst::ParameterInfo::kIsBypass)
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }

                if (paramInfo.flags & Vst::ParameterInfo::kIsHidden)
                {
                    p.hints.visible = false;
                }

                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );

                p.hints.plug_port_index = i;

                add_port( p );

                // Cache the port ID and index for easy lookup - only _control_ins
                if (have_control_in)
                {
                   // DMESSAGE( "Control input port \"%s\" ID %u",
                   //         utf16_to_utf8(paramInfo.title).c_str(), p.hints.parameter_id );

                    std::pair<int, unsigned long> prm ( int(p.hints.parameter_id), control_ins - 1 );
                    m_paramIds.insert(prm);
                }
            }
        }
    }
    
    DMESSAGE ("Control INS = %d: Control OUTS = %d", control_ins, control_outs);
}

void
VST3_Plugin::activate ( void )
{
    if ( !loaded() )
        return;
    
    if (m_processing)
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );

    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 0.0f;

    if ( ! _activated )
    {
        _activated = true;

	Vst::IComponent *component = m_component;
	if (component && m_processor)
        {
            vst3_activate(component, Vst::kAudio, Vst::kInput,  true);
            vst3_activate(component, Vst::kAudio, Vst::kOutput, true);
            vst3_activate(component, Vst::kEvent, Vst::kInput,  true);
            vst3_activate(component, Vst::kEvent, Vst::kOutput, true);
            component->setActive(true);
            m_processor->setProcessing(true);
            g_hostContext.processAddRef();
            m_processing = true;
        }
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST3_Plugin::deactivate ( void )
{
    if ( !loaded() )
        return;
    
    if (!m_processing)
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

   if ( _activated )
   {
        _activated = false;
        Vst::IComponent *component = m_component;
        if (component && m_processor)
        {
            g_hostContext.processReleaseRef();
            m_processor->setProcessing(false);
            component->setActive(false);
            m_processing = false;
            vst3_activate(component, Vst::kEvent, Vst::kOutput, false);
            vst3_activate(component, Vst::kEvent, Vst::kInput,  false);
            vst3_activate(component, Vst::kAudio, Vst::kOutput, false);
            vst3_activate(component, Vst::kAudio, Vst::kInput,  false);
        }
   }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST3_Plugin::vst3_activate ( Vst::IComponent *component,
	Vst::MediaType type, Vst::BusDirection direction, bool state )
{
    const int32 nbuses = component->getBusCount(type, direction);
    for (int32 i = 0; i < nbuses; ++i)
    {
        Vst::BusInfo busInfo;
        if (component->getBusInfo(type, direction, i, busInfo) == kResultOk)
        {
            if (busInfo.flags & Vst::BusInfo::kDefaultActive)
            {
                component->activateBus(type, direction, i, state);
            }
        }
    }
}

void
VST3_Plugin::add_port ( const Port &p )
{
    Module::add_port(p);

    if ( p.type() == Port::MIDI && p.direction() == Port::INPUT )
        midi_input.push_back( p );
    else if ( p.type() == Port::MIDI && p.direction() == Port::OUTPUT )
        midi_output.push_back( p );
}

void
VST3_Plugin::get ( Log_Entry &e ) const
{
    
}

void
VST3_Plugin::set ( Log_Entry &e )
{
    
}

#endif  // VST3_SUPPORT