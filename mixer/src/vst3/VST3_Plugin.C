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

#include "VST3_Plugin.H"

#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/gui/iplugview.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"

using namespace Steinberg;
using namespace Linux;


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
	void updateProcessContext(qtractorAudioEngine *pAudioEngine);

	// Cleanup.
	void clear();

protected:

	class PlugInterfaceSupport;

	class Attribute;
	class AttributeList;
	class Message;

	class Timer;

private:

	// Instance members.
	IPtr<PlugInterfaceSupport> m_plugInterfaceSupport;

	Timer *m_pTimer;

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

	QHash<ITimerHandler *, TimerHandlerItem *> m_timerHandlers;
	QList<TimerHandlerItem *> m_timerHandlerItems;

	QMultiHash<IEventHandler *, int> m_eventHandlers;

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
		if (m_fuids.contains(QString::fromLocal8Bit(_iid)))
			return kResultOk;
		else
			return kResultFalse;
	}

protected:

	void addPluInterfaceSupported(const TUID& _iid)
		{ m_fuids.append(QString::fromLocal8Bit(_iid)); }

private:

	// Instance members.
	QList<QString> m_fuids;
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
		qDeleteAll(m_list);
		m_list.clear();

		FUNKNOWN_DTOR
	}

	DECLARE_FUNKNOWN_METHODS

	//--- IAttributeList ---
	//
	tresult PLUGIN_API setInt (AttrID aid, int64 value) override
	{
		removeAttrID(aid);
		m_list.insert(aid, new Attribute(value));
		return kResultTrue;
	}

	tresult PLUGIN_API getInt (AttrID aid, int64& value) override
	{
		Attribute *attr = m_list.value(aid, nullptr);
		if (attr) {
			value = attr->intValue();
			return kResultTrue;
		}
		return kResultFalse;
	}

	tresult PLUGIN_API setFloat (AttrID aid, double value) override
	{
		removeAttrID(aid);
		m_list.insert(aid, new Attribute(value));
		return kResultTrue;
	}

	tresult PLUGIN_API getFloat (AttrID aid, double& value) override
	{
		Attribute *attr = m_list.value(aid, nullptr);
		if (attr) {
			value = attr->floatValue();
			return kResultTrue;
		}
		return kResultFalse;
	}

	tresult PLUGIN_API setString (AttrID aid, const Vst::TChar *string) override
	{
		removeAttrID(aid);
		m_list.insert(aid, new Attribute(string, fromTChar(string).length()));
		return kResultTrue;
	}

	tresult PLUGIN_API getString (AttrID aid, Vst::TChar *string, uint32 size) override
	{
		Attribute *attr = m_list.value(aid, nullptr);
		if (attr) {
			uint32 size2 = 0;
			const Vst::TChar *string2 = attr->stringValue(size2);
			::memcpy(string, string2, qMin(size, size2) * sizeof(Vst::TChar));
			return kResultTrue;
		}
		return kResultFalse;
	}

	tresult PLUGIN_API setBinary (AttrID aid, const void* data, uint32 size) override
	{
		removeAttrID(aid);
		m_list.insert(aid, new Attribute(data, size));
		return kResultTrue;
	}

	tresult PLUGIN_API getBinary (AttrID aid, const void*& data, uint32& size) override
	{
		Attribute *attr = m_list.value(aid, nullptr);
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
		Attribute *attr = m_list.value(aid, nullptr);
		if (attr) {
			delete attr;
			m_list.remove(aid);
		}
	}

private:

	// Instance members.
	QHash<QString, Attribute *> m_list;
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


//-----------------------------------------------------------------------------
// class qtractorVst3PluginHost -- VST3 plugin host context impl.
//

// Constructor.
qtractorVst3PluginHost::qtractorVst3PluginHost (void)
{
	FUNKNOWN_CTOR

	m_plugInterfaceSupport = owned(NEW PlugInterfaceSupport());

	m_pTimer = new Timer(this);

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

	delete m_pTimer;

	m_plugInterfaceSupport = nullptr;

	FUNKNOWN_DTOR
}



//--- IHostApplication ---
//
tresult PLUGIN_API qtractorVst3PluginHost::getName ( Vst::String128 name )
{
	const QString str("qtractorVst3PluginHost");
	const int nsize = qMin(str.length(), 127);
	::memcpy(name, str.utf16(), nsize * sizeof(Vst::TChar));
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
void qtractorVst3PluginHost::startTimer ( int msecs )
	{ if (++m_timerRefCount == 1) m_pTimer->start(msecs); }

void qtractorVst3PluginHost::stopTimer (void)
	{ if (m_timerRefCount > 0 && --m_timerRefCount == 0) m_pTimer->stop(); }

int qtractorVst3PluginHost::timerInterval (void) const
	{ return m_pTimer->interval(); }


// IRunLoop stuff...
//
tresult qtractorVst3PluginHost::registerEventHandler (
	IEventHandler *handler, FileDescriptor fd )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorVst3PluginHost::registerEventHandler(%p, %d)", handler, int(fd));
#endif
	m_eventHandlers.insert(handler, int(fd));
	return kResultOk;
}


tresult qtractorVst3PluginHost::unregisterEventHandler ( IEventHandler *handler )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorVst3PluginHost::unregisterEventHandler(%p)", handler);
#endif
	m_eventHandlers.remove(handler);
	return kResultOk;
}


tresult qtractorVst3PluginHost::registerTimer (
	ITimerHandler *handler, TimerInterval msecs )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorVst3PluginHost::registerTimer(%p, %u)", handler, uint(msecs));
#endif
	TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
	if (timer_handler) {
		timer_handler->reset(msecs);
	} else {
		timer_handler = new TimerHandlerItem(handler, msecs);
		m_timerHandlers.insert(handler, timer_handler);
	}
	m_pTimer->start(int(msecs));
	return kResultOk;
}


tresult qtractorVst3PluginHost::unregisterTimer ( ITimerHandler *handler )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorVst3PluginHost::unregisterTimer(%p)", handler);
#endif
	TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
	if (timer_handler) {
		m_timerHandlers.remove(handler);
		m_timerHandlerItems.append(timer_handler);
	}
	if (m_timerHandlers.isEmpty())
		m_pTimer->stop();
	return kResultOk;
}


// Executive methods.
//
void qtractorVst3PluginHost::processTimers (void)
{
	foreach (TimerHandlerItem *timer_handler, m_timerHandlers) {
		timer_handler->counter += timerInterval();
		if (timer_handler->counter >= timer_handler->interval) {
			timer_handler->handler->onTimer();
			timer_handler->counter = 0;
		}
	}
}


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
	qtractorAudioEngine *pAudioEngine )
{
	if (m_processRefCount < 1)
		return;

	const qtractorAudioEngine::TimeInfo& timeInfo
		= pAudioEngine->timeInfo();

	if (timeInfo.playing)
		m_processContext.state |=  Vst::ProcessContext::kPlaying;
	else
		m_processContext.state &= ~Vst::ProcessContext::kPlaying;

	m_processContext.sampleRate = timeInfo.sampleRate;
	m_processContext.projectTimeSamples = timeInfo.frame;

	m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid;
	m_processContext.projectTimeMusic = timeInfo.beats;
	m_processContext.state |= Vst::ProcessContext::kBarPositionValid;
	m_processContext.barPositionMusic = timeInfo.beats;

	m_processContext.state |= Vst::ProcessContext::kTempoValid;
	m_processContext.tempo  = timeInfo.tempo;
	m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
	m_processContext.timeSigNumerator = timeInfo.beatsPerBar;
	m_processContext.timeSigDenominator = timeInfo.beatType;
}


// Cleanup.
void qtractorVst3PluginHost::clear (void)
{
#ifdef CONFIG_VST3_XCB
	closeXcbConnection();
#endif

	m_timerRefCount = 0;
	m_processRefCount = 0;

	qDeleteAll(m_timerHandlerItems);
	m_timerHandlerItems.clear();
	m_timerHandlers.clear();

	m_eventHandlers.clear();

	::memset(&m_processContext, 0, sizeof(Vst::ProcessContext));
}


// Host singleton.
static qtractorVst3PluginHost g_hostContext;




VST3_Plugin::VST3_Plugin() :
    Plugin_Module(),
    m_module(nullptr),
    m_component(nullptr),
    m_controller(nullptr),
    m_unitInfos(nullptr),
    _plugin_filename()
{
    _plug_type = VST3;

    log_create();
}

VST3_Plugin::~VST3_Plugin()
{
    log_destroy();
}

bool
VST3_Plugin::load_plugin ( Module::Picked picked )
{
    _plugin_filename = picked.s_unique_id;
    
    if ( ! std::filesystem::exists(_plugin_filename) )
    {
        // FIXME check different location
        WARNING("Failed to find a suitable VST3 bundle binary %s", _plugin_filename.c_str());
        return false;
    }

    if ( !open_descriptor(0) )
    {
        DMESSAGE("Could not open descriptor %s", _plugin_filename.c_str());
        return false;
    }

    return false;
}

bool
VST3_Plugin::configure_inputs ( int )
{
    return false;
}

void
VST3_Plugin::handle_port_connection_change ( void )
{
    
}

void
VST3_Plugin::handle_chain_name_changed ( void )
{
    
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
    
}

void
VST3_Plugin::freeze_ports ( void )
{
    
}

void
VST3_Plugin::thaw_ports ( void )
{
    
}

void
VST3_Plugin::configure_midi_inputs ()
{
    
}

void
VST3_Plugin::configure_midi_outputs ()
{
    
}

nframes_t
VST3_Plugin::get_module_latency ( void ) const
{
    return 0;
}

void
VST3_Plugin::process ( nframes_t )
{
    
}

void
VST3_Plugin::handlePluginUIClosed()
{

}

void
VST3_Plugin::handlePluginUIResized(const uint width, const uint height)
{
    
}

// File loader.
bool
VST3_Plugin::open ( const std::string& sFilename )
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
    
    if (!open(_plugin_filename))
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
    #if 0
    if (factory3)
    {
        factory3->setHostContext(g_hostContext.get());   // FIXME
    }
    #endif
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
#if 0
            PClassInfoW classInfoW;
            if (factory3 && factory3->getClassInfoUnicode(n, &classInfoW) == kResultOk) {
                    m_sName = fromTChar(classInfoW.name);
                    m_sCategory = QString::fromLocal8Bit(classInfoW.category);
                    m_sSubCategories = QString::fromLocal8Bit(classInfoW.subCategories);
                    m_sVendor = fromTChar(classInfoW.vendor);
                    m_sVersion = fromTChar(classInfoW.version);
                    m_sSdkVersion = fromTChar(classInfoW.sdkVersion);
            } else {
                    PClassInfo2 classInfo2;
                    if (factory2 && factory2->getClassInfo2(n, &classInfo2) == kResultOk) {
                            m_sName = QString::fromLocal8Bit(classInfo2.name);
                            m_sCategory = QString::fromLocal8Bit(classInfo2.category);
                            m_sSubCategories = QString::fromLocal8Bit(classInfo2.subCategories);
                            m_sVendor = QString::fromLocal8Bit(classInfo2.vendor);
                            m_sVersion = QString::fromLocal8Bit(classInfo2.version);
                            m_sSdkVersion = QString::fromLocal8Bit(classInfo2.sdkVersion);
                    } else {
                            m_sName = QString::fromLocal8Bit(classInfo.name);
                            m_sCategory = QString::fromLocal8Bit(classInfo.category);
                            m_sSubCategories.clear();
                            m_sVendor.clear();
                            m_sVersion.clear();
                            m_sSdkVersion.clear();
                    }
            }

            if (m_sVendor.isEmpty())
                    m_sVendor = QString::fromLocal8Bit(factoryInfo.vendor);
            if (m_sEmail.isEmpty())
                    m_sEmail = QString::fromLocal8Bit(factoryInfo.email);
            if (m_sUrl.isEmpty())
                    m_sUrl = QString::fromLocal8Bit(factoryInfo.url);

            m_iUniqueID = qHash(QByteArray(classInfo.cid, sizeof(TUID)));
#endif
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
#if 0   // FIXME
            if (m_component->initialize(g_hostContext.get()) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to initialize plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                close();
                return false;
            }
#endif
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
#if 0   // FIXME
                    if (controller &&
                            controller->initialize(g_hostContext.get()) != kResultOk)
                    {
                        DMESSAGE("[%p]::open(\"%s\", %lu)"
                                " *** Failed to initialize plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                        controller = nullptr;
                    }
#endif
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
                    DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
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
VST3_Plugin::get ( Log_Entry &e ) const
{
    
}

void
VST3_Plugin::set ( Log_Entry &e )
{
    
}

#endif  // VST3_SUPPORT