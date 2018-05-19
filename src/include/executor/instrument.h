/*-------------------------------------------------------------------------
 *
 * instrument.h
 *	  definitions for run-time statistics collection
 *
 *
<<<<<<< HEAD
 * Portions Copyright (c) 2006-2009, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 * Copyright (c) 2001-2011, PostgreSQL Global Development Group
=======
 * Copyright (c) 2001-2012, PostgreSQL Global Development Group
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
 *
 * src/include/executor/instrument.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include "executor/executor.h"
#include "nodes/plannodes.h"
#include "portability/instr_time.h"
#include "utils/resowner.h"

struct CdbExplain_NodeSummary;          /* private def in cdb/cdbexplain.c */

typedef struct BufferUsage
{
	long		shared_blks_hit;	/* # of shared buffer hits */
	long		shared_blks_read;		/* # of shared disk blocks read */
	long		shared_blks_dirtied;	/* # of shared blocks dirtied */
	long		shared_blks_written;	/* # of shared disk blocks written */
	long		local_blks_hit; /* # of local buffer hits */
	long		local_blks_read;	/* # of local disk blocks read */
	long		local_blks_dirtied;		/* # of shared blocks dirtied */
	long		local_blks_written;		/* # of local disk blocks written */
	long		temp_blks_read; /* # of temp blocks read */
	long		temp_blks_written;		/* # of temp blocks written */
	instr_time	blk_read_time;	/* time spent reading */
	instr_time	blk_write_time; /* time spent writing */
} BufferUsage;

/* Flag bits included in InstrAlloc's instrument_options bitmask */
typedef enum InstrumentOption
{
<<<<<<< HEAD
	INSTRUMENT_NONE = 0,
	INSTRUMENT_TIMER = 1 << 0,	/* needs timer (and row counts) */
	INSTRUMENT_BUFFERS = 1 << 1,	/* needs buffer usage (not implemented yet) */
	INSTRUMENT_ROWS = 1 << 2,	/* needs row count */
	INSTRUMENT_CDB = 0x40000000,	/* needs cdb statistics */
=======
	INSTRUMENT_TIMER = 1 << 0,	/* needs timer (and row counts) */
	INSTRUMENT_BUFFERS = 1 << 1,	/* needs buffer usage */
	INSTRUMENT_ROWS = 1 << 2,	/* needs row count */
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
	INSTRUMENT_ALL = 0x7FFFFFFF
} InstrumentOption;

typedef struct Instrumentation
{
	/* Parameters set at node creation: */
	bool		need_timer;		/* TRUE if we need timer data */
<<<<<<< HEAD
	bool		need_cdb;		/* TRUE if we need cdb statistics */

=======
	bool		need_bufusage;	/* TRUE if we need buffer usage data */
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
	/* Info about current plan cycle: */
	bool		running;		/* TRUE if we've completed first tuple */
	instr_time	starttime;		/* Start time of current iteration of node */
	instr_time	counter;		/* Accumulated runtime for this node */
	double		firsttuple;		/* Time for first tuple of this cycle */
	uint64		tuplecount;		/* Tuples emitted so far this cycle */
	BufferUsage	bufusage_start;	/* Buffer usage at start */
	/* Accumulated statistics across all completed cycles: */
	double		startup;		/* Total startup time (in seconds) */
	double		total;			/* Total total time (in seconds) */
<<<<<<< HEAD
	uint64		ntuples;		/* Total tuples produced */
	uint64		nloops;			/* # of run cycles for this node */
	BufferUsage	bufusage;		/* Total buffer usage */

	double		execmemused;	/* CDB: executor memory used (bytes) */
	double		workmemused;	/* CDB: work_mem actually used (bytes) */
	double		workmemwanted;	/* CDB: work_mem to avoid scratch i/o (bytes) */
	instr_time	firststart;		/* CDB: Start time of first iteration of node */
	bool		workfileCreated;	/* TRUE if workfiles are created in this
									 * node */
	int			numPartScanned; /* Number of part tables scanned */
	const char *sortMethod;		/* CDB: Type of sort */
	const char *sortSpaceType;	/* CDB: Sort space type (Memory / Disk) */
	long		sortSpaceUsed;	/* CDB: Memory / Disk used by sort(KBytes) */
	struct CdbExplain_NodeSummary *cdbNodeSummary;	/* stats from all qExecs */
=======
	double		ntuples;		/* Total tuples produced */
	double		nloops;			/* # of run cycles for this node */
	double		nfiltered1;		/* # tuples removed by scanqual or joinqual */
	double		nfiltered2;		/* # tuples removed by "other" quals */
	BufferUsage bufusage;		/* Total buffer usage */
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
} Instrumentation;

extern PGDLLIMPORT BufferUsage pgBufferUsage;

extern Instrumentation *InstrAlloc(int n, int instrument_options);
extern void InstrStartNode(Instrumentation *instr);
extern void InstrStopNode(Instrumentation *instr, uint64 nTuples);
extern void InstrEndLoop(Instrumentation *instr);

/*
 * GPDB Note: Macro INSTR_START_NODE replaces InstrStartNode in ExecProcNode for
 * performance benefits, other files keep using InstrStartNode. Pay attention
 * to keep InstrStartNode/INSTR_START_NODE synchronized when modifying this macro.
 */
#define INSTR_START_NODE(instr) do {											\
	if ((instr)->need_timer) {													\
		if (INSTR_TIME_IS_ZERO((instr)->starttime))								\
			INSTR_TIME_SET_CURRENT((instr)->starttime);							\
		else																	\
			elog(DEBUG2, "INSTR_START_NODE called twice in a row");				\
	}																			\
} while(0)

/*
 * GPDB Note: Macro INSTR_STOP_NODE replaces InstrStopNode in ExecProcNode for
 * performance benefits, other files keep using InstrStopNode. Pay attention
 * to keep InstrStopNode/INSTR_STOP_NODE synchronized when modifying this macro.
 */
#define INSTR_STOP_NODE(instr, nTuples) do {									\
	(instr)->tuplecount += (nTuples);											\
	if ((instr)->need_timer)													\
	{																			\
		instr_time endtime;														\
		if (INSTR_TIME_IS_ZERO((instr)->starttime))								\
		{																		\
			elog(DEBUG2, "INSTR_STOP_NODE called without start");				\
			break;																\
		}																		\
		INSTR_TIME_SET_CURRENT(endtime);										\
		INSTR_TIME_ACCUM_DIFF((instr)->counter, endtime, (instr)->starttime);	\
		INSTR_TIME_SET_ZERO((instr)->starttime);								\
	}																			\
	if (!(instr)->running)														\
	{																			\
		(instr)->running = true;												\
		(instr)->firsttuple = INSTR_TIME_GET_DOUBLE((instr)->counter);			\
		(instr)->firststart = (instr)->starttime;								\
	}																			\
} while(0)

#define GP_INSTRUMENT_OPTS (gp_enable_query_metrics ? INSTRUMENT_ROWS : INSTRUMENT_NONE)

/* Greenplum query metrics */
typedef struct InstrumentationHeader
{
	void	   *head;
	int			free;
	slock_t		lock;
} InstrumentationHeader;

typedef struct InstrumentationSlot
{
	Instrumentation data;
	int32		pid;			/* process id */
	int32		tmid;			/* transaction time */
	int32		ssid;			/* session id */
	int32		ccnt;			/* command count */
	int16		segid;			/* segment id */
	int16		nid;			/* node id */
} InstrumentationSlot;

/*
 * To guarantee the slot recycled properly,
 * record the slot with its resource owner when picked
 */
typedef struct InstrumentationResownerSet
{
	InstrumentationSlot *slot;
	ResourceOwner owner;
	struct InstrumentationResownerSet *next;
} InstrumentationResownerSet;

extern InstrumentationHeader *InstrumentGlobal;
extern Size InstrShmemNumSlots(void);
extern Size InstrShmemSize(void);
extern void InstrShmemInit(void);
extern Instrumentation *GpInstrAlloc(const Plan *node, int instrument_options);

/*
 * For each free slot in shmem, fill it with specific pattern
 * Use this pattern to detect the slot has been recycled.
 * Also protect writes outside the allocated shmem buffer.
 */
#define PATTERN 0xd5
#define LONG_PATTERN 0xd5d5d5d5d5d5d5d5

/*
 * Empty if first 8 bytes of slot filled with pattern.
 */
#define SlotIsEmpty(slot) ((*((int64 *)(slot)) ^ LONG_PATTERN) == 0)

/*
 * The last 8 bytes of slot points to next free slot.
 */
#define GetInstrumentNext(slot) (*((InstrumentationSlot **)((slot) + 1) - 1))

/*
 * Limit the maximum scan node's instr per query in shmem
 */
#define MAX_SCAN_ON_SHMEM 300

#endif   /* INSTRUMENT_H */
