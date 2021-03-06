/*-------------------------------------------------------------------------
 *
 * index.h
 *	  prototypes for catalog/index.c.
 *
 *
 * Portions Copyright (c) 1996-2010, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/catalog/index.h,v 1.83 2010/02/07 22:40:33 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEX_H
#define INDEX_H

#include "access/relscan.h"     /* Relation, Snapshot */
#include "executor/tuptable.h"  /* TupTableSlot */
#include "nodes/execnodes.h"

struct EState;                  /* #include "nodes/execnodes.h" */

#define DEFAULT_INDEX_TYPE	"btree"

/* Typedef for callback function for IndexBuildScan */
typedef void (*IndexBuildCallback) (Relation index,
									ItemPointer tupleId,
									Datum *values,
									bool *isnull,
									bool tupleIsAlive,
									void *state);

/* Action code for index_set_state_flags */
typedef enum
{
	INDEX_CREATE_SET_READY,
	INDEX_CREATE_SET_VALID
} IndexStateFlagsAction;


extern Oid index_create(Oid heapRelationId,
			 const char *indexRelationName,
			 Oid indexRelationId,
			 IndexInfo *indexInfo,
			 List *indexColNames,
			 Oid accessMethodObjectId,
			 Oid tableSpaceId,
			 Oid *classObjectId,
			 int16 *coloptions,
			 Datum reloptions,
			 bool isprimary,
			 bool isconstraint,
			 bool deferrable,
			 bool initdeferred,
			 bool allow_system_table_mods,
			 bool skip_build,
			 bool concurrent,
			 const char *altConName);

extern void index_drop(Oid indexId);

extern IndexInfo *BuildIndexInfo(Relation index);

extern void FormIndexDatum(IndexInfo *indexInfo,
			   TupleTableSlot *slot,
			   struct EState *estate,
			   Datum *values,
			   bool *isnull);

extern Oid setNewRelfilenodeToOid(Relation relation, TransactionId freezeXid,
					   Oid newrelfilenode);

extern void index_build(Relation heapRelation,
			Relation indexRelation,
			IndexInfo *indexInfo,
			bool isprimary,
			bool isreindex);

extern double IndexBuildScan(Relation parentRelation,
					Relation indexRelation,
					IndexInfo *indexInfo,
					bool allow_sync,
					IndexBuildCallback callback,
					void *callback_state);

extern void validate_index(Oid heapId, Oid indexId, Snapshot snapshot);

extern void index_set_state_flags(Oid indexId, IndexStateFlagsAction action);

extern void reindex_index(Oid indexId, bool skip_constraint_checks);
extern bool reindex_relation(Oid relid, bool toast_too, bool heap_rebuilt);

extern bool ReindexIsProcessingHeap(Oid heapOid);
extern bool ReindexIsProcessingIndex(Oid indexOid);

extern Oid IndexGetRelation(Oid indexId);

#endif   /* INDEX_H */
