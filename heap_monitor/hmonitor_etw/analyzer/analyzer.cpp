#include "stdafx.h"
#include "analyzer.h"
#include "symbol.h"
#include "track_system/track_system.h"

#include "jtwsm/fixed_priority_queue.h"
#include "utility/qpc_helper.h"
#include <math.h>


TSSAnalyzer::TSSAnalyzer()
	: m_limitTop(6)
	, m_sortType(ST_CountTotal)
	, m_wthPid()
	, m_wthPname()
	, m_wthAllocStat()
	, m_pid()
	, m_process(NULL)
	, m_wthStackid()
	, m_wthImgid()
	, m_wthHeapid()
	, m_wthTid()
{
}

TSSAnalyzer::~TSSAnalyzer()
{
}


void TSSAnalyzer::UpdateSnapshot()
{
	getTrackSystem()->Snapshot(m_sys);
	calcIDWidth<10>(m_sys.processes, m_wthPid);
	calcPnameWidth(m_sys.processes, m_wthPname);
	calcAllocStatWidth(m_sys.processes, m_wthAllocStat);

	tst_pid pid = m_pid;
	m_pid = 0;
	m_process = NULL;

	// default
	if (pid == 0 && !m_sys.processes.empty())
		pid = m_sys.processes[0].id;

	if (pid != 0)
		SetTargetProcess(pid);
}

bool TSSAnalyzer::SetTargetProcess(tst_pid pid)
{
	TSS_System::ProcessS::const_iterator itor =
		binary_find(m_sys.processes, pid);
	if (itor != m_sys.processes.end())
	{
		m_pid = pid;
		m_process = &(*itor);

		m_wthStackid = 16;
		calcIDWidth<16>(m_process->images, m_wthImgid);
		calcIDWidth<16>(m_process->heaps, m_wthHeapid);
		calcIDWidth<10>(m_process->threads, m_wthTid);
		return true;
	}
	return false;
}

tst_pid TSSAnalyzer::TargetProcess() const
{
	return m_pid;
}

bool TSSAnalyzer::Command(int argc, wchar_t** argv)
{
	if (_wcsicmp(argv[0], L"processlist") == 0 ||
		_wcsicmp(argv[0], L"pl") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showProcessList(m_sys.processes);
		return true;
	}
	if (_wcsicmp(argv[0], L"processlistext") == 0 ||
		_wcsicmp(argv[0], L"ple") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showProcessList(m_sys.processes, true);
		return true;
	}

	if (m_process == NULL)
	{
		printf("Unknown process\n");
		return true;
	}

	if (_wcsicmp(argv[0], L"stacklist") == 0 ||
		_wcsicmp(argv[0], L"sl") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showStackList(m_process->stacks);
		return true;
	}
	if (_wcsicmp(argv[0], L"stacklistext") == 0 ||
		_wcsicmp(argv[0], L"sle") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showStackList(m_process->stacks, true);
		return true;
	}
	if (_wcsicmp(argv[0], L"imagelist") == 0 ||
		_wcsicmp(argv[0], L"il") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showImageList(m_process->images);
		return true;
	}
	if (_wcsicmp(argv[0], L"imagelistext") == 0 ||
		_wcsicmp(argv[0], L"ile") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showImageList(m_process->images, true);
		return true;
	}
	if (_wcsicmp(argv[0], L"heaplist") == 0 ||
		_wcsicmp(argv[0], L"hl") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showHeapList(m_process->heaps);
		return true;
	}
	if (_wcsicmp(argv[0], L"heaplistext") == 0 ||
		_wcsicmp(argv[0], L"hle") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showHeapList(m_process->heaps, true);
		return true;
	}
	if (_wcsicmp(argv[0], L"threadlist") == 0 ||
		_wcsicmp(argv[0], L"tl") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showThreadList(m_process->threads);
		return true;
	}
	if (_wcsicmp(argv[0], L"threadlistext") == 0 ||
		_wcsicmp(argv[0], L"tle") == 0)
	{
		parseSortArgs(argc - 1, argv + 1, m_limitTop, m_sortType);
		showThreadList(m_process->threads, true);
		return true;
	}

	return false;
}


void TSSAnalyzer::showSnapshotInfo()
{
	printf("[Snapshot, %d seconds ago]\n",
		(int)g_QPCHelper.GetTimeSpentMS(m_sys.tsLast) / 1000);
}

void TSSAnalyzer::showSortInfo()
{
	const char* szST[] =
	{
		"CountTotal", "CountCurrent", "CountPeak",
		"BytesTotal", "BytesCurrent", "BytesPeak"
	};
	printf("[Show Top %u, sort by %s]\n", m_limitTop, szST[m_sortType]);
}

void TSSAnalyzer::showProcessList(
	const TSS_System::ProcessS &pl, bool fExt)
{
	showSnapshotInfo();
	showSortInfo();
	printf("Process List\n");
	printf("\n");

	if (pl.empty())
		return;

	std::vector<const TSS_Process*> ss;
	sortSetByLimit(pl, ss, m_limitTop, m_sortType);

	for (size_t i = 0; i < ss.size(); ++i)
	{
		const TSS_Process &p = *(ss[i]);

		printf("%-*u  %*s  ",
			m_wthPid, p.id, m_wthPname, p.name.c_str());
		printAllocStat(p, m_wthAllocStat, "  ");
		printf("\n");
		printf("\n");
	}
}

void TSSAnalyzer::showStackList(
	const TSS_Process::StackS &sl, bool fExt)
{
	showSnapshotInfo();
	showSortInfo();
	printf("Stack List\n");
	printf("\n");

	if (sl.empty())
		return;

	std::vector<const TSS_Stack*> ss;
	sortSetByLimit(sl, ss, m_limitTop, m_sortType);

	for (size_t i = 0; i < ss.size(); ++i)
	{
		const TSS_Stack &s = *(ss[i]);

		printf("0x%-*llx  ", m_wthStackid, s.id);
		printAllocStat(s, m_wthAllocStat, "  ");
		printf("\n");

		if (fExt)
		{
			printf("Stack info:\n");
			printIRAS(*(s.ira_syms));
		}

		printf("\n");
	}
}

void TSSAnalyzer::showImageList(
	const TSS_Process::ImageS &il, bool fExt)
{
	showSnapshotInfo();
	showSortInfo();
	printf("Image List\n");
	printf("\n");

	if (il.empty())
		return;

	std::vector<const TSS_Image*> ss;
	sortSetByLimit(il, ss, m_limitTop, m_sortType);

	for (size_t i = 0; i < ss.size(); ++i)
	{
		const TSS_Image &img = *(ss[i]);

		printf("%s\n", m_sys.nimImg.name(img.index));

		printf("0x%-*llx  ", m_wthImgid, img.id);
		printAllocStat(img, m_wthAllocStat, "  ");
		printf("\n");

		if (fExt)
		{
			printf("ImageLoad Stack info:\n");
			printIRAS(*(img.stackLoad));
		}

		printf("\n");
	}
}

void TSSAnalyzer::showHeapList(
	const TSS_Process::HeapS &hl, bool fExt)
{
	showSnapshotInfo();
	showSortInfo();
	printf("Heap List\n");
	printf("\n");

	if (hl.empty())
		return;

	std::vector<const TSS_Heap*> ss;
	sortSetByLimit(hl, ss, m_limitTop, m_sortType);

	for (size_t i = 0; i < ss.size(); ++i)
	{
		const TSS_Heap &h = *(ss[i]);

		printf("0x%-*llx  ", m_wthHeapid, h.id);
		printAllocStat(h, m_wthAllocStat, "  ");
		printf("\n");

		if (fExt)
		{
			printf("HeapCreate Stack info:\n");
			printIRAS(*(h.stackCreate));
		}

		printf("\n");
	}
}

void TSSAnalyzer::showThreadList(
	const TSS_Process::ThreadS &tl, bool fExt)
{
	showSnapshotInfo();
	showSortInfo();
	printf("Thread List\n");
	printf("\n");

	if (tl.empty())
		return;

	std::vector<const TSS_Thread*> ss;
	sortSetByLimit(tl, ss, m_limitTop, m_sortType);

	for (size_t i = 0; i < ss.size(); ++i)
	{
		const TSS_Thread &t = *(ss[i]);

		printf("%-*u  ", m_wthTid, t.id);
		printAllocStat(t, m_wthAllocStat, "  ");
		printf("\n");

		printIRA(t.entry);
		printf("\n");

		if (fExt)
		{
			printf("CreateThread Stack info:\n");
			printIRAS(*(t.stackStart));
		}

		printf("\n");
	}
}


void TSSAnalyzer::parseSortArgs(
	int argc, wchar_t** argv, size_t &top, int &st)
{
	if (argc >= 1)
	{
		int tmp = _wtoi(argv[0]);
		if (tmp > 0)
			top = tmp;
	}
	if (argc >= 2)
	{
		if (_wcsicmp(argv[1], L"CountTotal") == 0)
		{
			st = ST_CountTotal;
		}
		else if(_wcsicmp(argv[1], L"CountCurrent") == 0)
		{
			st = ST_CountCurrent;
		}
		else if(_wcsicmp(argv[1], L"CountPeak") == 0)
		{
			st = ST_CountPeak;
		}
		else if(_wcsicmp(argv[1], L"BytesTotal") == 0)
		{
			st = ST_BytesTotal;
		}
		else if(_wcsicmp(argv[1], L"BytesCurrent") == 0)
		{
			st = ST_BytesCurrent;
		}
		else if(_wcsicmp(argv[1], L"BytesPeak") == 0)
		{
			st = ST_BytesPeak;
		}
	}
}

template <class S, class SQ>
static void sortUseQueue(const S &s, SQ &sq)
{
	for (S::const_iterator itor = s.begin(); itor != s.end(); ++itor)
	{
		sq.push(&(*itor));
	}
	sq.sort();
}

template <class S, class CPS>
void TSSAnalyzer::sortSetByLimit(const S &s, CPS &out, size_t top, int st)
{
	top = min(top, s.size());

	typedef typename CPS::value_type CP;
	switch (st)
	{
	case ST_CountTotal:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_CountTotal> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	case ST_CountCurrent:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_CountCurrent> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	case ST_CountPeak:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_CountPeak> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	case ST_BytesTotal:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_BytesTotal> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	case ST_BytesCurrent:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_BytesCurrent> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	case ST_BytesPeak:
	{
		FixedPriorityQueue<CP, CPS, CompT<ST_BytesPeak> > topQueue(top, out);
		sortUseQueue(s, topQueue);
		break;
	}
	default:
		ASSERT (0);
	}
}

template <size_t Base, class S>
void TSSAnalyzer::calcIDWidth(const S &s, size_t &wthID)
{
	typedef typename S::value_type::ID ID;
	ID idMax = ID();
	for (S::const_iterator itor = s.begin();
		itor != s.end(); ++itor)
	{
		idMax = max(idMax, itor->id);
	}
	wthID = (size_t)(log(idMax) / log(Base)) + 1;
}

void TSSAnalyzer::calcPnameWidth(
	const TSS_System::ProcessS &pl, size_t &wthPname)
{
	wthPname = 0;
	for (TSS_System::ProcessS::const_iterator itor = pl.begin();
		itor != pl.end(); ++itor)
	{
		wthPname = max(wthPname, itor->name.length());
	}
}

template <class S>
void TSSAnalyzer::calcAllocStatWidth(const S &s, size_t wthAllocStat[6])
{
	AllocCount acMax;
	AllocBytes abMax;
	for (S::const_iterator itor = s.begin();
		itor != s.end(); ++itor)
	{
		const AllocCount &ac = (*itor).StatBy<AllocCountIdx>();
		acMax.total = max(acMax.total, ac.total);
		acMax.current = max(acMax.current, ac.current);
		acMax.peak = max(acMax.peak, ac.peak);
		const AllocBytes &ab = (*itor).StatBy<AllocBytesIdx>();
		abMax.total = max(abMax.total, ab.total);
		abMax.current = max(abMax.current, ab.current);
		abMax.peak = max(abMax.peak, ab.peak);
	}
	wthAllocStat[0] = (size_t)log10(acMax.total) + 1;
	wthAllocStat[1] = (size_t)log10(acMax.current) + 1;
	wthAllocStat[2] = (size_t)log10(acMax.peak) + 1;
	wthAllocStat[3] = (size_t)log10(abMax.total) + 1;
	wthAllocStat[4] = (size_t)log10(abMax.current) + 1;
	wthAllocStat[5] = (size_t)log10(abMax.peak) + 1;
}

template <class T>
void TSSAnalyzer::printAllocStat(
	const T &t, size_t wthAllocStat[6], const char* delimiter)
{
	const AllocCount &ac = t.StatBy<AllocCountIdx>();
	const AllocBytes &ab = t.StatBy<AllocBytesIdx>();
	printf(
		"(%*llu, %*llu, %*llu)%s(%*llu, %*llu, %*llu)",
		wthAllocStat[0], ac.total,
		wthAllocStat[1], ac.current,
		wthAllocStat[2], ac.peak,
		delimiter,
		wthAllocStat[3], ab.total,
		wthAllocStat[4], ab.current,
		wthAllocStat[5], ab.peak
		);
}

void TSSAnalyzer::printIRA(const IRA_Sym &ira_sym)
{
	if (!ira_sym.sym.empty())
	{
		printf("%s", ira_sym.sym.c_str());
		return;
	}

	IRA ira = ira_sym.ira;
	if (!ira.valid && m_process != NULL)
	{
		ira = m_process->addr2IRA(ira.u64);
	}

	if (ira.valid)
	{
		const char* name = m_sys.nimImg.name(ira.index);
		symLoadModule(name);
		char funInfo[512];
		getFuncInfo(name, ira.offset, funInfo);
		ira_sym.sym = funInfo;
	}
	else
	{
		char funInfo[512];
		sprintf(funInfo, "0x%llx", ira.u64);
		ira_sym.sym = funInfo;
	}

	printf("%s", ira_sym.sym.c_str());
	return;
}

void TSSAnalyzer::printIRAS(
	const IRA_SymS &ira_syms,
	const char* prefix,
	const char* postfix
	)
{
	for (IRA_SymS::const_iterator itor = ira_syms.begin();
		itor != ira_syms.end(); ++itor)
	{
		printf("%s", prefix);
		printIRA(*itor);
		printf("%s", postfix);
	}
}