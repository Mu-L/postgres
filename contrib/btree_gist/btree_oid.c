/*
 * contrib/btree_gist/btree_oid.c
 */
#include "postgres.h"

#include "btree_gist.h"
#include "btree_utils_num.h"
#include "utils/sortsupport.h"

typedef struct
{
	Oid			lower;
	Oid			upper;
} oidKEY;

/* GiST support functions */
PG_FUNCTION_INFO_V1(gbt_oid_compress);
PG_FUNCTION_INFO_V1(gbt_oid_fetch);
PG_FUNCTION_INFO_V1(gbt_oid_union);
PG_FUNCTION_INFO_V1(gbt_oid_picksplit);
PG_FUNCTION_INFO_V1(gbt_oid_consistent);
PG_FUNCTION_INFO_V1(gbt_oid_distance);
PG_FUNCTION_INFO_V1(gbt_oid_penalty);
PG_FUNCTION_INFO_V1(gbt_oid_same);
PG_FUNCTION_INFO_V1(gbt_oid_sortsupport);


static bool
gbt_oidgt(const void *a, const void *b, FmgrInfo *flinfo)
{
	return (*((const Oid *) a) > *((const Oid *) b));
}
static bool
gbt_oidge(const void *a, const void *b, FmgrInfo *flinfo)
{
	return (*((const Oid *) a) >= *((const Oid *) b));
}
static bool
gbt_oideq(const void *a, const void *b, FmgrInfo *flinfo)
{
	return (*((const Oid *) a) == *((const Oid *) b));
}
static bool
gbt_oidle(const void *a, const void *b, FmgrInfo *flinfo)
{
	return (*((const Oid *) a) <= *((const Oid *) b));
}
static bool
gbt_oidlt(const void *a, const void *b, FmgrInfo *flinfo)
{
	return (*((const Oid *) a) < *((const Oid *) b));
}

static int
gbt_oidkey_cmp(const void *a, const void *b, FmgrInfo *flinfo)
{
	oidKEY	   *ia = (oidKEY *) (((const Nsrt *) a)->t);
	oidKEY	   *ib = (oidKEY *) (((const Nsrt *) b)->t);

	if (ia->lower == ib->lower)
	{
		if (ia->upper == ib->upper)
			return 0;

		return (ia->upper > ib->upper) ? 1 : -1;
	}

	return (ia->lower > ib->lower) ? 1 : -1;
}

static float8
gbt_oid_dist(const void *a, const void *b, FmgrInfo *flinfo)
{
	Oid			aa = *(const Oid *) a;
	Oid			bb = *(const Oid *) b;

	if (aa < bb)
		return (float8) (bb - aa);
	else
		return (float8) (aa - bb);
}


static const gbtree_ninfo tinfo =
{
	gbt_t_oid,
	sizeof(Oid),
	8,							/* sizeof(gbtreekey8) */
	gbt_oidgt,
	gbt_oidge,
	gbt_oideq,
	gbt_oidle,
	gbt_oidlt,
	gbt_oidkey_cmp,
	gbt_oid_dist
};


PG_FUNCTION_INFO_V1(oid_dist);
Datum
oid_dist(PG_FUNCTION_ARGS)
{
	Oid			a = PG_GETARG_OID(0);
	Oid			b = PG_GETARG_OID(1);
	Oid			res;

	if (a < b)
		res = b - a;
	else
		res = a - b;
	PG_RETURN_OID(res);
}


/**************************************************
 * GiST support functions
 **************************************************/

Datum
gbt_oid_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(gbt_num_compress(entry, &tinfo));
}

Datum
gbt_oid_fetch(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(gbt_num_fetch(entry, &tinfo));
}

Datum
gbt_oid_consistent(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	Oid			query = PG_GETARG_OID(1);
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);

	/* Oid		subtype = PG_GETARG_OID(3); */
	bool	   *recheck = (bool *) PG_GETARG_POINTER(4);
	oidKEY	   *kkk = (oidKEY *) DatumGetPointer(entry->key);
	GBT_NUMKEY_R key;

	/* All cases served by this function are exact */
	*recheck = false;

	key.lower = (GBT_NUMKEY *) &kkk->lower;
	key.upper = (GBT_NUMKEY *) &kkk->upper;

	PG_RETURN_BOOL(gbt_num_consistent(&key, &query, &strategy,
									  GIST_LEAF(entry), &tinfo, fcinfo->flinfo));
}

Datum
gbt_oid_distance(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	Oid			query = PG_GETARG_OID(1);

	/* Oid		subtype = PG_GETARG_OID(3); */
	oidKEY	   *kkk = (oidKEY *) DatumGetPointer(entry->key);
	GBT_NUMKEY_R key;

	key.lower = (GBT_NUMKEY *) &kkk->lower;
	key.upper = (GBT_NUMKEY *) &kkk->upper;

	PG_RETURN_FLOAT8(gbt_num_distance(&key, &query, GIST_LEAF(entry),
									  &tinfo, fcinfo->flinfo));
}

Datum
gbt_oid_union(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	void	   *out = palloc(sizeof(oidKEY));

	*(int *) PG_GETARG_POINTER(1) = sizeof(oidKEY);
	PG_RETURN_POINTER(gbt_num_union(out, entryvec, &tinfo, fcinfo->flinfo));
}

Datum
gbt_oid_penalty(PG_FUNCTION_ARGS)
{
	oidKEY	   *origentry = (oidKEY *) DatumGetPointer(((GISTENTRY *) PG_GETARG_POINTER(0))->key);
	oidKEY	   *newentry = (oidKEY *) DatumGetPointer(((GISTENTRY *) PG_GETARG_POINTER(1))->key);
	float	   *result = (float *) PG_GETARG_POINTER(2);

	penalty_num(result, origentry->lower, origentry->upper, newentry->lower, newentry->upper);

	PG_RETURN_POINTER(result);
}

Datum
gbt_oid_picksplit(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(gbt_num_picksplit((GistEntryVector *) PG_GETARG_POINTER(0),
										(GIST_SPLITVEC *) PG_GETARG_POINTER(1),
										&tinfo, fcinfo->flinfo));
}

Datum
gbt_oid_same(PG_FUNCTION_ARGS)
{
	oidKEY	   *b1 = (oidKEY *) PG_GETARG_POINTER(0);
	oidKEY	   *b2 = (oidKEY *) PG_GETARG_POINTER(1);
	bool	   *result = (bool *) PG_GETARG_POINTER(2);

	*result = gbt_num_same((void *) b1, (void *) b2, &tinfo, fcinfo->flinfo);
	PG_RETURN_POINTER(result);
}

static int
gbt_oid_ssup_cmp(Datum x, Datum y, SortSupport ssup)
{
	oidKEY	   *arg1 = (oidKEY *) DatumGetPointer(x);
	oidKEY	   *arg2 = (oidKEY *) DatumGetPointer(y);

	/* for leaf items we expect lower == upper, so only compare lower */
	if (arg1->lower > arg2->lower)
		return 1;
	else if (arg1->lower < arg2->lower)
		return -1;
	else
		return 0;
}

Datum
gbt_oid_sortsupport(PG_FUNCTION_ARGS)
{
	SortSupport ssup = (SortSupport) PG_GETARG_POINTER(0);

	ssup->comparator = gbt_oid_ssup_cmp;
	ssup->ssup_extra = NULL;

	PG_RETURN_VOID();
}
