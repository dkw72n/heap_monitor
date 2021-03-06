#include "stdafx.h"
#include "etw_session.h"
#include "utility/qpc_helper.h"


ETWSession::ETWSession()
{
	m_fRealtime = true;
	m_filter = NULL;
	m_etwProperties = NULL;

	m_lastTS = 0;
	m_maxDur = 0;

	m_wildStack = 0;

	m_total = 0;
	m_success = 0;
	m_parseFailed = 0;
	m_filtered = 0;
	m_unknownProvider = 0;
	m_eventIgnore = 0;
	m_versionIgnore = 0;
}

ETWSession::~ETWSession()
{
	if (m_etwProperties != NULL)
	{
		free(m_etwProperties);
		m_etwProperties = NULL;
	}

	EventQueue::BlockList bl;
	m_evtQueue.pop_all(bl);
	for (EventQueue::BlockList::iterator itor = bl.begin();
		itor != bl.end(); ++itor)
	{
		EventQueue::Block &b = *itor;
		for (EventQueue::Block::iterator itorb = b.begin();
			itorb != b.end(); ++itorb)
		{
			IEvent* evt = itorb->inner();
			evt->destroy();
		}
	}
}

bool ETWSession::Init(
	bool fRealtime,
	LPCWSTR sessionName,
	IEventFilter* filter /* = NULL */
	)
{
	m_fRealtime = fRealtime;
	m_sessionName = sessionName;
	m_filter = filter;

	size_t bufSize = sizeof(EVENT_TRACE_PROPERTIES) +
		(1024 * sizeof(WCHAR)) +
		(1024 * sizeof(WCHAR));

	m_etwProperties = (EVENT_TRACE_PROPERTIES*)malloc(bufSize);
	memset(m_etwProperties, 0, bufSize);
	m_etwProperties->Wnode.BufferSize = (ULONG)bufSize;

	ULONG status = ::ControlTraceW(
		(TRACEHANDLE)NULL,
		m_sessionName.c_str(),
		m_etwProperties,
		EVENT_TRACE_CONTROL_QUERY
		);

	return (status == ERROR_SUCCESS);
}

bool ETWSession::Flush()
{
	ULONG status = ::ControlTraceW(
		(TRACEHANDLE)NULL,
		m_sessionName.c_str(),
		m_etwProperties,
		EVENT_TRACE_CONTROL_FLUSH
		);

	return (status == ERROR_SUCCESS);
}

void ETWSession::PopStales(
	int64_t staleTime, EventQueue::BlockList &bl)
{
	AutoLock _alock(&m_lock);

	m_evtQueue.set_staletime(staleTime);
	m_evtQueue.pop_stales(bl);
}


void ETWSession::newEvent(IEvent* evt)
{
	AutoLock _alock(&m_lock);

	if (!m_evtQueue.push(Event(evt)))
	{
		evt->destroy();
	}
}

void ETWSession::newStack(
	int64_t ts,
	uint32_t pid, uint32_t tid,
	size_t depth, uint32_t* frames
	)
{
	const uint64_t* constFrames = NULL;
	uint64_t id = m_stackTable.append(depth, frames, constFrames);

	newStack(ts, pid, tid, id, depth, constFrames);
}

void ETWSession::newStack(
	int64_t ts,
	uint32_t pid, uint32_t tid,
	size_t depth, uint64_t* frames
	)
{
	const uint64_t* constFrames = NULL;
	uint64_t id = m_stackTable.append(depth, frames, constFrames);

	newStack(ts, pid, tid, id, depth, constFrames);
}

void ETWSession::newStack(
	int64_t ts,
	uint32_t pid, uint32_t tid,
	uint64_t id, size_t depth, const uint64_t* frames
	)
{
	AutoLock _alock(&m_lock);

	const size_t MAX_SEARCH = 256;

	bool fWild = true;

	const EventQueue::Container &c = m_evtQueue.container();
	EventQueue::Container::const_reverse_iterator itor = c.rbegin();
	for (size_t i = 0; i < MAX_SEARCH && itor != c.rend(); ++i, ++itor)
	{
		const Event &evt = *itor;

		if (evt.createtime() == ts)
		{
			if (evt.inner()->pid() == pid &&
				evt.inner()->tid() == tid)
			{
				evt.inner()->setStack(id, depth, frames);
				fWild = false;
			}
		}
		else if (evt.createtime() < ts)
		{
			break;
		}
	}

	if (fWild)
		++m_wildStack;
}


unsigned ETWSession::ThreadWorker()
{
	EVENT_TRACE_LOGFILEW trace;
    TRACE_LOGFILE_HEADER* pHeader = &trace.LogfileHeader;

    ZeroMemory(&trace, sizeof(EVENT_TRACE_LOGFILE));
	if (m_fRealtime)
	{
		 trace.LoggerName = (LPWSTR)m_sessionName.c_str();
		 trace.ProcessTraceMode = PROCESS_TRACE_MODE_RAW_TIMESTAMP |
			PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_REAL_TIME;
	}
	else
	{
		trace.LogFileName = (LPWSTR)m_sessionName.c_str();
		 trace.ProcessTraceMode = PROCESS_TRACE_MODE_RAW_TIMESTAMP |
			PROCESS_TRACE_MODE_EVENT_RECORD;
	}
	trace.EventRecordCallback = (PEVENT_RECORD_CALLBACK)(ProcessEvent);
	trace.Context = this;

    TRACEHANDLE hTrace = ::OpenTraceW(&trace);
    if (hTrace == INVALID_PROCESSTRACE_HANDLE)
    {
		return GetLastError();
    }

    TDHSTATUS status = ::ProcessTrace(&hTrace, 1, 0, 0);

	::CloseTrace(hTrace);

    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED)
    {
		return status;
    }

	return 0;
}

VOID ETWSession::ProcessEvent(PEVENT_RECORD pEvent)
{
	ETWSession* pThis = (ETWSession*)(pEvent->UserContext);
	pThis->processEvent(pEvent);
}

void ETWSession::processEvent(PEVENT_RECORD pEvent)
{
	m_total++;

	switch (::EventCreate(pEvent, this, m_filter))
	{
	case CREATE_SUCCESS:
		m_success++;
		break;
	case CREATE_PARSE_FAILED:
		m_parseFailed++;
		break;
	case CREATE_FILTERED:
		m_filtered++;
		break;
	case CREATE_UNKNOWN_PROVIDER:
		m_unknownProvider++;
		break;
	case CREATE_IGNORE_EVENT:
		m_eventIgnore++;
		break;
	case CREATE_IGNORE_VERSION:
		m_versionIgnore++;
		break;
	}

	m_lastTS = pEvent->EventHeader.TimeStamp.QuadPart;

	int64_t dur = g_QPCHelper.GetQPCNow() - m_lastTS;
	if (dur > m_maxDur)
		m_maxDur = dur;
}


#define MAXIMUM_SESSION_NAME 1024

// KERNEL_LOGGER_NAMEA
// KERNEL_LOGGER_NAMEW
#define HEAP_LOGGER_NAMEA   "Heap Logger"
#define HEAP_LOGGER_NAMEW  L"Heap Logger"

PEVENT_TRACE_PROPERTIES
AllocateTraceProperties (
	LPCWSTR LoggerName,
	LPCWSTR LogFileName
	)
{
	PEVENT_TRACE_PROPERTIES TraceProperties = NULL;
	ULONG BufferSize;

	BufferSize = sizeof(EVENT_TRACE_PROPERTIES) +
		(MAXIMUM_SESSION_NAME + MAX_PATH) * sizeof(WCHAR);

	TraceProperties = (PEVENT_TRACE_PROPERTIES)malloc(BufferSize);

	ZeroMemory(TraceProperties, BufferSize);
	TraceProperties->Wnode.BufferSize = BufferSize;
	TraceProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
	TraceProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
	TraceProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) +
    (MAXIMUM_SESSION_NAME * sizeof(WCHAR));

	if (LoggerName != NULL) {
		wcscpy((LPWSTR)((PCHAR)TraceProperties + TraceProperties->LoggerNameOffset), LoggerName);
	}

	if (LogFileName != NULL) {
		wcscpy((LPWSTR)((PCHAR)TraceProperties + TraceProperties->LogFileNameOffset), LogFileName);
	}

	return TraceProperties;
}

VOID
FreeTraceProperties (
	PEVENT_TRACE_PROPERTIES TraceProperties
	)
{
	free(TraceProperties);
	return;
}

EXTERN_C const GUID FAR ProcessTraceGuid;
EXTERN_C const GUID FAR ImageTraceGuid;
EXTERN_C const GUID FAR ThreadTraceGuid;
EXTERN_C const GUID FAR HeapTraceGuid;

bool StartKernelTrace(PEVENT_TRACE_PROPERTIES &TraceProperties)
{
	bool fRet = false;
	bool fTraceStarted = false;

	TraceProperties = AllocateTraceProperties(KERNEL_LOGGER_NAMEW, NULL);

	TraceProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
	TraceProperties->Wnode.ClientContext = 1; // Use QueryPerformanceCounter for time stamps
	TraceProperties->Wnode.Guid = SystemTraceControlGuid;
	TraceProperties->EnableFlags = EVENT_TRACE_FLAG_PROCESS | EVENT_TRACE_FLAG_THREAD | EVENT_TRACE_FLAG_IMAGE_LOAD;
	TraceProperties->BufferSize = 64;
	TraceProperties->MinimumBuffers = 64;
	TraceProperties->MaximumBuffers = 256;

	TRACEHANDLE SessionHandle = 0;
	ULONG Status = StartTraceW(&SessionHandle, KERNEL_LOGGER_NAMEW, TraceProperties);
	if (Status != ERROR_SUCCESS) {
		goto Exit;
	}

	fTraceStarted = true;

	CLASSIC_EVENT_ID EventId[2];
	ZeroMemory(EventId, sizeof(EventId));
	EventId[0].EventGuid = ImageTraceGuid;
	EventId[0].Type =  EVENT_TRACE_TYPE_LOAD;
	EventId[1].EventGuid = ThreadTraceGuid;
	EventId[1].Type = EVENT_TRACE_TYPE_START;

	Status = TraceSetInformation(SessionHandle,
								 TraceStackTracingInfo,
								 EventId,
								 sizeof(EventId));

	if (Status != ERROR_SUCCESS) {
		goto Exit;
	}

	fRet = true;

Exit:
	if (!fRet) {
		if (fTraceStarted) {
			ControlTraceW(NULL, KERNEL_LOGGER_NAMEW, TraceProperties, EVENT_TRACE_CONTROL_STOP);
		}

		FreeTraceProperties(TraceProperties);
		TraceProperties = NULL;
	}

	return fRet;
}

void StopKernelTrace(PEVENT_TRACE_PROPERTIES &TraceProperties)
{
	ControlTraceW(NULL, KERNEL_LOGGER_NAMEW, TraceProperties, EVENT_TRACE_CONTROL_STOP);
	FreeTraceProperties(TraceProperties);
	TraceProperties = NULL;
}
