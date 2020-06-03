/*-------------------------------------------------------------------------
 *
 * parse_partition_gp.c
 *	  Expand GPDB legacy partition syntax to PostgreSQL commands.
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *	src/backend/parser/parse_partition_gp.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/table.h"
#include "catalog/partition.h"
#include "catalog/pg_collation.h"
#include "cdb/cdbvars.h"
#include "commands/defrem.h"
#include "commands/tablecmds.h"
#include "executor/execPartition.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/parsenodes.h"
#include "nodes/primnodes.h"
#include "parser/parse_expr.h"
#include "parser/parse_oper.h"
#include "parser/parse_utilcmd.h"
#include "partitioning/partdesc.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/partcache.h"
#include "utils/rel.h"
#include "utils/ruleutils.h"

typedef struct
{
	PartitionKeyData *partkey;
	Datum		endVal;

	ExprState   *plusexprstate;
	ParamListInfo plusexpr_params;
	EState	   *estate;

	Datum		currStart;
	Datum		currEnd;
	bool		called;
	bool		endReached;

	/* for context in error messages */
	ParseState *pstate;
	int			end_location;
	int			every_location;
} PartEveryIterator;

static char *ChoosePartitionName(Relation parentrel, const char *levelstr,
								 const char *partname, int partnum);

static List *generateRangePartitions(ParseState *pstate,
									 Relation parentrel,
									 GpPartDefElem *elem,
									 PartitionSpec *subPart,
									 partname_comp *partnamecomp);
static List *generateListPartition(ParseState *pstate,
								   Relation parentrel,
								   GpPartDefElem *elem,
								   PartitionSpec *subPart,
								   partname_comp *partnamecomp);
static List *generateDefaultPartition(ParseState *pstate,
									  Relation parentrel,
									  GpPartDefElem *elem,
									  PartitionSpec *subPart,
									  partname_comp *partnamecomp);

static char *extract_tablename_from_options(List **options);

/*
 * qsort_stmt_cmp
 *
 * Used when sorting CreateStmts across all partitions.
 */
static int32
qsort_stmt_cmp(const void *a, const void *b, void *arg)
{
	int32		cmpval = 0;
	CreateStmt	   *b1cstmt = (CreateStmt *) lfirst(*(ListCell **) a);
	CreateStmt	   *b2cstmt = (CreateStmt *) lfirst(*(ListCell **) b);
	PartitionKey partKey = (PartitionKey) arg;
	PartitionBoundSpec *b1 = b1cstmt->partbound;
	PartitionBoundSpec *b2 = b2cstmt->partbound;
	int partnatts = partKey->partnatts;
	FmgrInfo *partsupfunc = partKey->partsupfunc;
	Oid *partcollation = partKey->partcollation;
	int			i;
	List	   *b1lowerdatums = b1->lowerdatums;
	List	   *b2lowerdatums = b2->lowerdatums;
	List	   *b1upperdatums = b1->upperdatums;
	List	   *b2upperdatums = b2->upperdatums;

	/* Sort DEFAULT partitions last */
	if (b1->is_default != b2->is_default)
	{
		if (b2->is_default)
			return 1;
		else
			return -1;
	}
	else if (b1lowerdatums != NULL && b2lowerdatums != NULL)
	{
		for (i = 0; i < partnatts; i++)
		{
			ListCell *lc;
			Const *n;
			Datum b1lowerdatum;
			Datum b2lowerdatum;

			lc = list_nth_cell(b1lowerdatums, i);
			n = lfirst(lc);
			b1lowerdatum = n->constvalue;

			lc = list_nth_cell(b2lowerdatums, i);
			n = lfirst(lc);
			b2lowerdatum = n->constvalue;

			cmpval = DatumGetInt32(FunctionCall2Coll(&partsupfunc[i],
													 partcollation[i],
													 b1lowerdatum,
													 b2lowerdatum));
			if (cmpval != 0)
				break;
		}
	}
	else if (b1upperdatums != NULL && b2upperdatums != NULL)
	{
		for (i = 0; i < partnatts; i++)
		{
			ListCell *lc;
			Const *n;
			Datum b1upperdatum;
			Datum b2upperdatum;

			lc = list_nth_cell(b1upperdatums, i);
			n = lfirst(lc);
			b1upperdatum = n->constvalue;

			lc = list_nth_cell(b2upperdatums, i);
			n = lfirst(lc);
			b2upperdatum = n->constvalue;

			cmpval = DatumGetInt32(FunctionCall2Coll(&partsupfunc[i],
													 partcollation[i],
													 b1upperdatum,
													 b2upperdatum));
			if (cmpval != 0)
				break;
		}
	}
	else if (b1lowerdatums != NULL && b2upperdatums != NULL)
	{
		for (i = 0; i < partnatts; i++)
		{
			ListCell *lc;
			Const *n;
			Datum b1lowerdatum;
			Datum b2upperdatum;

			lc = list_nth_cell(b1lowerdatums, i);
			n = lfirst(lc);
			b1lowerdatum = n->constvalue;

			lc = list_nth_cell(b2upperdatums, i);
			n = lfirst(lc);
			b2upperdatum = n->constvalue;

			cmpval = DatumGetInt32(FunctionCall2Coll(&partsupfunc[i],
													 partcollation[i],
													 b1lowerdatum,
													 b2upperdatum));
			if (cmpval != 0)
				break;
		}

		/*
		 * if after comparison, b1 lower and b2 upper are same, we should get
		 * b2 before b1 so that its start can be adjusted properly. Hence,
		 * return b1 is greater than b2 to flip the order.
		 */
		if (cmpval == 0)
			cmpval = 1;
	}
	else if (b1upperdatums != NULL && b2lowerdatums != NULL)
	{
		for (i = 0; i < partnatts; i++)
		{
			ListCell *lc;
			Const *n;
			Datum b1upperdatum;
			Datum b2lowerdatum;

			lc = list_nth_cell(b1upperdatums, i);
			n = lfirst(lc);
			b1upperdatum = n->constvalue;

			lc = list_nth_cell(b2lowerdatums, i);
			n = lfirst(lc);
			b2lowerdatum = n->constvalue;

			cmpval = DatumGetInt32(FunctionCall2Coll(&partsupfunc[i],
													 partcollation[i],
													 b1upperdatum,
													 b2lowerdatum));
			if (cmpval != 0)
				break;
		}
	}

	return cmpval;
}

/*
 * Sort the list of GpPartitionBoundSpecs based first on the START, if START
 * does not exists, use END. After sort, if any stmt contains an implicit START
 * or END, deduce the value and update the corresponding list of CreateStmts.
 */
static List *
deduceImplicitRangeBounds(ParseState *pstate, List *origstmts, PartitionKey key)
{
	List *stmts;
	ListCell *lc;
	CreateStmt *prevstmt = NULL;

	stmts = list_qsort(origstmts, qsort_stmt_cmp, key);

	foreach(lc, stmts)
	{
		Node	   *n = lfirst(lc);
		CreateStmt *stmt;

		Assert(IsA(n, CreateStmt));
		stmt = (CreateStmt *) n;
		if (!stmt->partbound->lowerdatums)
		{
			if (prevstmt)
				stmt->partbound->lowerdatums = prevstmt->partbound->upperdatums;
			else
			{
				ColumnRef  *minvalue = makeNode(ColumnRef);
				minvalue->location = -1;
				minvalue->fields = lcons(makeString("minvalue"), NIL);
				stmt->partbound->lowerdatums = list_make1(minvalue);
			}

		}
		if (!stmt->partbound->upperdatums)
		{
			Node *next = lc->next ? lfirst(lc->next) : NULL;
			if (next)
			{
				CreateStmt *nextstmt = (CreateStmt *)next;
				stmt->partbound->upperdatums = nextstmt->partbound->lowerdatums;
			}
			else
			{
				ColumnRef  *maxvalue = makeNode(ColumnRef);
				maxvalue->location = -1;
				maxvalue->fields = lcons(makeString("maxvalue"), NIL);
				stmt->partbound->upperdatums = list_make1(maxvalue);
			}
		}
		prevstmt = stmt;
	}
	return stmts;
}

static ExprState *
initPlusExprState(ParseState *pstate, EState *estate, const char *part_col_name,
				  Oid part_col_typid, int32 part_col_typmod,
				  Oid part_col_collation, Node *interval)
{
	Node          *plusexpr;
	Param         *param;
	ParamListInfo plusexpr_params;

	/*
	 * NOTE: We don't use transformPartitionBoundValue() here. We don't want to cast
	 * the EVERY clause to that type; rather, we'll be passing it to the + operator.
	 * For example, if the partition column is a timestamp, the EVERY clause
	 * can be an interval, so don't try to cast it to timestamp.
	 */

	param = makeNode(Param);
	param->paramkind = PARAM_EXTERN;
	param->paramid = 1;
	param->paramtype = part_col_typid;
	param->paramtypmod = part_col_typmod;
	param->paramcollid = part_col_collation;
	param->location = -1;

	/* Look up + operator */
	plusexpr = (Node *) make_op(pstate,
								list_make2(makeString("pg_catalog"), makeString("+")),
								(Node *) param,
								(Node *) transformExpr(pstate, interval, EXPR_KIND_PARTITION_BOUND),
								pstate->p_last_srf,
								-1);

	/*
	 * Check that the input expression's collation is compatible with one
	 * specified for the parent's partition key (partcollation).  Don't throw
	 * an error if it's the default collation which we'll replace with the
	 * parent's collation anyway.
	 */
	if (IsA(plusexpr, CollateExpr))
	{
		Oid			exprCollOid = exprCollation(plusexpr);

		if (OidIsValid(exprCollOid) &&
			exprCollOid != DEFAULT_COLLATION_OID &&
			exprCollOid != part_col_collation)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
						errmsg("collation of partition bound value for column \"%s\" does not match partition key collation \"%s\"",
							   part_col_name, get_collation_name(part_col_collation))));
	}

	plusexpr = coerce_to_target_type(pstate,
									 plusexpr, exprType(plusexpr),
									 part_col_typid,
									 part_col_typmod,
									 COERCION_ASSIGNMENT,
									 COERCE_IMPLICIT_CAST,
									 -1);
	if (plusexpr == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
					errmsg("specified value cannot be cast to type %s for column \"%s\"",
						   format_type_be(part_col_typid), part_col_name)));

	plusexpr_params = makeParamList(1);
	plusexpr_params->params[0].value = (Datum) 0;
	plusexpr_params->params[0].isnull = true;
	plusexpr_params->params[0].pflags = 0;
	plusexpr_params->params[0].ptype = part_col_typid;

	estate->es_param_list_info = plusexpr_params;

	return ExecInitExprWithParams((Expr *) plusexpr, plusexpr_params);
}

/*
 * Functions for iterating through all the partition bounds based on
 * START/END/EVERY.
 */
static PartEveryIterator *
initPartEveryIterator(ParseState *pstate, PartitionKeyData *partkey, const char *part_col_name,
					  Node *start, Node *end, bool endIncl, Node *every)
{
	PartEveryIterator *iter;
	Datum		startVal = 0;
	Datum		endVal = 0;
	Datum		everyVal;
	Oid			plusop;
	Oid			part_col_typid;
	int32		part_col_typmod;
	Oid			part_col_collation;

	/* caller should've checked this already */
	Assert(partkey->partnatts == 1);

	part_col_typid = get_partition_col_typid(partkey, 0);
	part_col_typmod = get_partition_col_typmod(partkey, 0);
	part_col_collation = get_partition_col_collation(partkey, 0);

	/* Parse the START/END/EVERY clauses */
	if (start)
	{
		Const	   *startConst;

		startConst = transformPartitionBoundValue(pstate,
												  start,
												  part_col_name,
												  part_col_typid,
												  part_col_typmod,
												  part_col_collation);
		if (startConst->constisnull)
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot use NULL with range partition specification"),
				 parser_errposition(pstate, exprLocation(start))));

		startVal = startConst->constvalue;
	}

	if (end)
	{
		Const	   *endConst;

		endConst = transformPartitionBoundValue(pstate,
												end,
												part_col_name,
												part_col_typid,
												part_col_typmod,
												part_col_collation);
		if (endConst->constisnull)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						errmsg("cannot use NULL with range partition specification"),
						parser_errposition(pstate, exprLocation(end))));

		if (endIncl)
		{
			ExprState	*plusexprstate;
			EState		*estate;
			A_Const		*one;
			Datum 		endplusone;
			bool		isnull;

			one = makeNode(A_Const);
			one->val.type = T_Integer;
			one->val.val.ival = 1;
			one->location = -1;
			estate = CreateExecutorState();
			plusexprstate = initPlusExprState(pstate,
											  estate,
											  part_col_name,
											  part_col_typid,
											  part_col_typmod,
											  part_col_collation,
											  (Node *) one);

			estate->es_param_list_info->params[0].isnull = false;
			estate->es_param_list_info->params[0].value = endConst->constvalue;
			endplusone = ExecEvalExprSwitchContext(plusexprstate,
												   GetPerTupleExprContext(estate),
												   &isnull);
			if (isnull)
				elog(ERROR, "plus-operator returned NULL"); // GPDB_12_MERGE_FIXME: better message

			endVal = endplusone;
		}
		else
			endVal = endConst->constvalue;
	}

	iter = palloc0(sizeof(PartEveryIterator));
	iter->partkey = partkey;
	iter->endVal = endVal;

	if (every)
	{
		if (start == NULL || end == NULL)
		{
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("EVERY clause requires START and END"),
				 parser_errposition(pstate, exprLocation(every))));
		}

		iter->estate = CreateExecutorState();
		iter->plusexprstate = initPlusExprState(pstate,
												iter->estate,
												part_col_name,
												part_col_typid,
												part_col_typmod,
												part_col_collation,
												every);
		iter->plusexpr_params = iter->estate->es_param_list_info;
	}
	else
	{
		everyVal = (Datum) 0;
		plusop = InvalidOid;
	}

	iter->currEnd = startVal;
	iter->currStart = (Datum) 0;
	iter->called = false;
	iter->endReached = false;

	iter->pstate = pstate;
	iter->end_location = exprLocation(end);
	iter->every_location = exprLocation(every);

	return iter;
}

static void
freePartEveryIterator(PartEveryIterator *iter)
{
	if (iter->estate)
		FreeExecutorState(iter->estate);
}

/*
 * Return next partition bound in START/END/EVERY specification.
 */
static bool
nextPartBound(PartEveryIterator *iter)
{
	bool		firstcall;

	firstcall = !iter->called;
	iter->called = true;

	if (iter->plusexprstate)
	{
		/*
		 * Call (previous bound) + EVERY
		 */
		Datum		next;
		int32		cmpval;
		bool		isnull;

		/* If the previous partition reached END, we're done */
		if (iter->endReached)
			return false;

		iter->plusexpr_params->params[0].isnull = false;
		iter->plusexpr_params->params[0].value = iter->currEnd;

		next = ExecEvalExprSwitchContext(iter->plusexprstate,
										 GetPerTupleExprContext(iter->estate),
										 &isnull);
		if (isnull)
			elog(ERROR, "plus-operator returned NULL"); // GPDB_12_MERGE_FIXME: better message

		iter->currStart = iter->currEnd;

		/* Is the next bound greater than END? */
		cmpval = DatumGetInt32(FunctionCall2Coll(&iter->partkey->partsupfunc[0],
												 iter->partkey->partcollation[0],
												 next,
												 iter->endVal));
		if (cmpval >= 0)
		{
			iter->endReached = true;
			iter->currEnd = iter->endVal;
		}
		else
		{
			/*
			 * Sanity check that the next bound is > previous bound. This prevents us
			 * from getting into an infinite loop if the + operator is not behaving.
			 */
			cmpval = DatumGetInt32(FunctionCall2Coll(&iter->partkey->partsupfunc[0],
													 iter->partkey->partcollation[0],
													 iter->currEnd,
													 next));
			if (cmpval >= 0)
			{
				if (firstcall)
				{
					/*
					 * Second iteration: parameter hasn't increased the
					 * current end from the old end.
					 */
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("EVERY parameter too small"),
							 parser_errposition(iter->pstate, iter->every_location)));
				}
				else
				{
					/*
					 * We got a smaller value but later than we
					 * thought so it must be an overflow.
					 */
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("END parameter not reached before type overflows"),
							 parser_errposition(iter->pstate, iter->end_location)));
				}
			}

			iter->currEnd = next;
		}

		return true;
	}
	else
	{
		/* Without EVERY, create just one partition that covers the whole range */
		if (!firstcall)
			return false;

		iter->called = true;
		iter->currStart = iter->currEnd;
		iter->currEnd = iter->endVal;
		return true;
	}
}

static char *
ChoosePartitionName(Relation parentrel, const char *levelstr,
					const char *partname, int partnum)
{
	char partsubstring[NAMEDATALEN];

	if (partname)
	{
		snprintf(partsubstring, NAMEDATALEN, "prt_%s", partname);
		return makeObjectName(RelationGetRelationName(parentrel),
							  levelstr,
							  partsubstring);
	}

	Assert(partnum > 0);
	snprintf(partsubstring, NAMEDATALEN, "prt_%d", partnum);
	return ChooseRelationName(RelationGetRelationName(parentrel),
							  levelstr,
							  partsubstring,
							  RelationGetNamespace(parentrel),
							  false);
}

CreateStmt *
makePartitionCreateStmt(Relation parentrel, char *partname, PartitionBoundSpec *boundspec,
						PartitionSpec *subPart, GpPartDefElem *elem,
						partname_comp *partnamecomp)
{
	CreateStmt *childstmt;
	RangeVar   *parentrv;
	RangeVar   *childrv;
	char	   *schemaname;
	const char *final_part_name;
	char levelStr[NAMEDATALEN];

	snprintf(levelStr, NAMEDATALEN, "%d", partnamecomp->level);
	if (partnamecomp->tablename)
		final_part_name = partnamecomp->tablename;
	else
		final_part_name = ChoosePartitionName(parentrel, levelStr, partname,
											  ++partnamecomp->partnum);

	schemaname = get_namespace_name(parentrel->rd_rel->relnamespace);
	parentrv = makeRangeVar(schemaname, pstrdup(RelationGetRelationName(parentrel)), -1);
	parentrv->relpersistence = parentrel->rd_rel->relpersistence;

	childrv = makeRangeVar(schemaname, (char *)final_part_name, -1);
	childrv->relpersistence = parentrel->rd_rel->relpersistence;

	childstmt = makeNode(CreateStmt);
	childstmt->relation = childrv;
	childstmt->tableElts = NIL;
	childstmt->inhRelations = list_make1(parentrv);
	childstmt->partbound = boundspec;
	childstmt->partspec = subPart;
	childstmt->ofTypename = NULL;
	childstmt->constraints = NIL;
	childstmt->options = elem->options ? copyObject(elem->options) : NIL;
	childstmt->oncommit = ONCOMMIT_NOOP;  // FIXME: copy from parent stmt?
	childstmt->tablespacename = elem->tablespacename ? pstrdup(elem->tablespacename) : NULL;
	childstmt->accessMethod = elem->accessMethod ? pstrdup(elem->accessMethod) : NULL;
	childstmt->if_not_exists = false;
	childstmt->distributedBy = make_distributedby_for_rel(parentrel);
	childstmt->partitionBy = NULL;
	childstmt->relKind = 0;
	childstmt->ownerid = parentrel->rd_rel->relowner;

	return childstmt;
}

/* Generate partitions for START (..) END (..) EVERY (..) */
static List *
generateRangePartitions(ParseState *pstate,
						Relation parentrel,
						GpPartDefElem *elem,
						PartitionSpec *subPart,
						partname_comp *partnamecomp)
{
	GpPartitionRangeSpec *boundspec;
	List				 *result = NIL;
	PartitionKey		 partkey;
	char				 *partcolname;
	PartEveryIterator	 *boundIter;
	Node				 *start = NULL;
	Node				 *end = NULL;
	bool				 endIncl = false;
	Node				 *every = NULL;
	int					 i;

	if (elem->boundSpec == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("missing boundary specification in partition \"%s\" of type RANGE",
						elem->partName),
				 parser_errposition(pstate, elem->location)));

	if (!IsA(elem->boundSpec, GpPartitionRangeSpec))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("invalid boundary specification for RANGE partition"),
				 parser_errposition(pstate, elem->location)));

	boundspec = (GpPartitionRangeSpec *) elem->boundSpec;
	partkey = RelationGetPartitionKey(parentrel);

	/*
	 * GPDB_12_MERGE_FIXME: We currently disabled support for multi column
	 * range partitioned tables. PostgreSQL doesn't support that. Not sure
	 * what to do about that.  Add support for it to PostgreSQL? Simplify the
	 * grammar to not allow that?
	 */
	if (partkey->partnatts != 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("too many columns for RANGE partition -- only one column is allowed")));

	/* Syntax doesn't allow expressions in partition key */
	Assert(partkey->partattrs[0] != 0);
	partcolname = NameStr(TupleDescAttr(RelationGetDescr(parentrel), partkey->partattrs[0] - 1)->attname);

	if (boundspec->partStart)
	{
		if (list_length(boundspec->partStart) != partkey->partnatts)
			elog(ERROR, "invalid number of start values"); // GPDB_12_MERGE_FIXME: improve message
		start = linitial(boundspec->partStart);
	}

	if (boundspec->partEnd)
	{
		if (list_length(boundspec->partEnd) != partkey->partnatts)
			elog(ERROR, "invalid number of end values"); // GPDB_12_MERGE_FIXME: improve message
		end = linitial(boundspec->partEnd);
		endIncl = boundspec->partEndEdge == PART_EDGE_INCLUSIVE ? true : false;
		elog(NOTICE, "partEndIncl is %d", endIncl);
	}

	/*
	 * Tablename is used by legacy dump and restore ONLY. If tablename is
	 * specified expectation is to ignore the EVERY clause even if
	 * specified. Ideally, dump should never dump the partition CREATE stmts
	 * with EVERY clause, but somehow old code didn't remove EVERY clause from
	 * dump instead ignored the same during restores. Hence, we need to carry
	 * the same hack in new code.
	 */
	if (partnamecomp->tablename == NULL && boundspec->partEvery)
	{
		if (list_length(boundspec->partEvery) != partkey->partnatts)
			elog(ERROR, "invalid number of every values"); // GPDB_12_MERGE_FIXME: improve message
		every = linitial(boundspec->partEvery);
	}

	partkey = RelationGetPartitionKey(parentrel);

	boundIter = initPartEveryIterator(pstate, partkey, partcolname, start, end, endIncl, every);

	i = 0;
	while (nextPartBound(boundIter))
	{
		PartitionBoundSpec *boundspec;
		CreateStmt *childstmt;
		char	   *partname;
		char partsubstring[NAMEDATALEN];

		boundspec = makeNode(PartitionBoundSpec);
		boundspec->strategy = PARTITION_STRATEGY_RANGE;
		boundspec->is_default = false;
		if (start)
			boundspec->lowerdatums = list_make1(makeConst(boundIter->partkey->parttypid[0],
													  boundIter->partkey->parttypmod[0],
													  boundIter->partkey->parttypcoll[0],
													  boundIter->partkey->parttyplen[0],
													  datumCopy(boundIter->currStart,
																boundIter->partkey->parttypbyval[0],
																boundIter->partkey->parttyplen[0]),
													  false,
													  boundIter->partkey->parttypbyval[0]));
		if (end)
			boundspec->upperdatums = list_make1(makeConst(boundIter->partkey->parttypid[0],
													  boundIter->partkey->parttypmod[0],
													  boundIter->partkey->parttypcoll[0],
													  boundIter->partkey->parttyplen[0],
													  datumCopy(boundIter->currEnd,
																boundIter->partkey->parttypbyval[0],
																boundIter->partkey->parttyplen[0]),
													  false,
													  boundIter->partkey->parttypbyval[0]));
		boundspec->location = -1;

		if (every && elem->partName)
		{
			snprintf(partsubstring, NAMEDATALEN, "%s_%d", elem->partName, ++i);
			partname = &partsubstring[0];
		}
		else
			partname = elem->partName;

		childstmt = makePartitionCreateStmt(parentrel, partname, boundspec,
											subPart, elem, partnamecomp);
		result = lappend(result, childstmt);
	}

	freePartEveryIterator(boundIter);

	return result;
}

static List *
generateListPartition(ParseState *pstate,
					  Relation parentrel,
					  GpPartDefElem *elem,
					  PartitionSpec *subPart,
					  partname_comp *partnamecomp)
{
	GpPartitionListSpec *gpvaluesspec;
	PartitionBoundSpec  *boundspec;
	CreateStmt			*childstmt;
	PartitionKey		partkey;
	ListCell			*lc;
	List				*listdatums;

	if (elem->boundSpec == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					  errmsg("missing boundary specification in partition \"%s\" of type LIST",
							 elem->partName),
							 parser_errposition(pstate, elem->location)));

	if (!IsA(elem->boundSpec, GpPartitionListSpec))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					  errmsg("invalid boundary specification for LIST partition"),
					  parser_errposition(pstate, elem->location)));

	gpvaluesspec = (GpPartitionListSpec *) elem->boundSpec;

	partkey = RelationGetPartitionKey(parentrel);

	boundspec = makeNode(PartitionBoundSpec);
	boundspec->strategy = PARTITION_STRATEGY_LIST;
	boundspec->is_default = false;

	/*
	 * GPDB_12_MERGE_FIXME: Greenplum historically does not support multi column
	 * List partitions. Upstream Postgres allows it. Keep this restriction for
	 * now and most likely we will get the functionality for free from the merge
	 * and we should remove this restriction once we verifies that.
	 */

	listdatums = NIL;
	foreach (lc, gpvaluesspec->partValues)
	{
		List	   *thisvalue = lfirst(lc);

		if (list_length(thisvalue) != 1)
			elog(ERROR, "VALUES specification with more than one column not allowed");

		listdatums = lappend(listdatums, linitial(thisvalue));
	}

	boundspec->listdatums = listdatums;
	boundspec->location = -1;

	boundspec = transformPartitionBound(pstate, parentrel, boundspec);
	childstmt = makePartitionCreateStmt(parentrel, elem->partName, boundspec, subPart,
										elem, partnamecomp);

	return list_make1(childstmt);
}

static List *
generateDefaultPartition(ParseState *pstate,
						 Relation parentrel,
						 GpPartDefElem *elem,
						 PartitionSpec *subPart,
						 partname_comp *partnamecomp)
{
	PartitionBoundSpec *boundspec;
	CreateStmt *childstmt;

	boundspec = makeNode(PartitionBoundSpec);
	boundspec->is_default = true;
	boundspec->location = -1;

	/* default partition always needs name to be specified */
	Assert(elem->partName != NULL);
	childstmt = makePartitionCreateStmt(parentrel, elem->partName, boundspec, subPart,
										elem, partnamecomp);
	return list_make1(childstmt);
}

static char *
extract_tablename_from_options(List **options)
{
	ListCell *o_lc;
	ListCell *prev_lc = NULL;
	char *tablename = NULL;

	foreach (o_lc, *options)
	{
		DefElem    *pDef = (DefElem *) lfirst(o_lc);

		/*
		 * get the tablename from the WITH, then remove this element
		 * from the list
		 */
		if (0 == strcmp(pDef->defname, "tablename"))
		{
			/* if the string isn't quoted you get a typename ? */
			if (!IsA(pDef->arg, String))
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("invalid tablename specification")));

			char *relname_str = defGetString(pDef);
			*options = list_delete_cell(*options, o_lc, prev_lc);
			tablename = pstrdup(relname_str);
			break;
		}
		prev_lc = o_lc;
	}

	return tablename;
}

/*
 * Create a list of CreateStmts, to create partitions based on 'gpPartDef'
 * specification.
 */
List *
generatePartitions(Oid parentrelid, GpPartitionDefinition *gpPartSpec,
				   PartitionSpec *subPartSpec, const char *queryString,
				   List *parentoptions, const char *parentaccessmethod)
{
	Relation	parentrel;
	List	   *result = NIL;
	ParseState *pstate;
	ListCell	*lc;
	List	   *ancestors = get_partition_ancestors(parentrelid);
	partname_comp partcomp = {.tablename=NULL, .level=0, .partnum=0};
	bool isSubTemplate;

	partcomp.level = list_length(ancestors) + 1;

	pstate = make_parsestate(NULL);
	pstate->p_sourcetext = queryString;

	parentrel = table_open(parentrelid, NoLock);

	/* Remove "tablename" cell from parentOptions, if exists */
	extract_tablename_from_options(&parentoptions);

	if (subPartSpec)
	{
		if (subPartSpec->gpPartDef)
		{
			Assert(subPartSpec->gpPartDef->istemplate);
			isSubTemplate = subPartSpec->gpPartDef->istemplate;
		}
		else
			isSubTemplate = false;
	}

	foreach(lc, gpPartSpec->partDefElems)
	{
		Node	   *n = lfirst(lc);

		if (IsA(n, GpPartDefElem))
		{
			GpPartDefElem *elem           = (GpPartDefElem *) n;
			List          *new_parts;
			PartitionSpec *tmpSubPartSpec = NULL;

			if (subPartSpec)
			{
				tmpSubPartSpec = copyObject(subPartSpec);
				if (!isSubTemplate)
					tmpSubPartSpec->gpPartDef = (GpPartitionDefinition*) elem->subSpec;

				if (tmpSubPartSpec->gpPartDef == NULL)
				{
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
								errmsg("no partitions specified at depth %d",
									   partcomp.level + 1),
								parser_errposition(pstate, subPartSpec->location)));
				}
			}

			/* if WITH has "tablename" then it will be used as name for partition */
			partcomp.tablename = extract_tablename_from_options(&elem->options);

			if (elem->options == NIL)
				elem->options = parentoptions ? copyObject(parentoptions) : NIL;
			if (elem->accessMethod == NULL)
				elem->accessMethod = parentaccessmethod ? pstrdup(parentaccessmethod) : NULL;

			if (elem->isDefault)
				new_parts = generateDefaultPartition(pstate, parentrel, elem, tmpSubPartSpec, &partcomp);
			else
			{
				PartitionKey key = RelationGetPartitionKey(parentrel);
				Assert(key != NULL);
				switch (key->strategy)
				{
					case PARTITION_STRATEGY_RANGE:
						new_parts = generateRangePartitions(pstate, parentrel, elem, tmpSubPartSpec, &partcomp);
						break;

					case PARTITION_STRATEGY_LIST:
						new_parts = generateListPartition(pstate, parentrel, elem, tmpSubPartSpec, &partcomp);
						break;
					default:
						elog(ERROR, "Not supported partition strategy");
				}
			}

			result = list_concat(result, new_parts);
		}
		else if (IsA(n, ColumnReferenceStorageDirective))
		{
			/* GPDB_12_MERGE_FIXME */
			elog(ERROR, "column storage directives not implemented yet");
		}
	}

	/*
	 * Validate and maybe update range partitions bound here instead of in
	 * check_new_partition_bound(), because we need to modify the lower or upper
	 * bounds for implicit START/END.
	 */
	/* GPDB range partition */
	PartitionKey key = RelationGetPartitionKey(parentrel);
	if (key->strategy == PARTITION_STRATEGY_RANGE)
		result = deduceImplicitRangeBounds(pstate, result, key);

	free_parsestate(pstate);
	table_close(parentrel, NoLock);
	return result;
}
