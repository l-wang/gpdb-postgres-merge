/*
 * contrib/xml2/xslt_proc.c
 *
 * XSLT processing functions (requiring libxslt)
 *
 * John Gray, for Torchbox 2003-04-01
 */
#include "postgres.h"

#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/xml.h"

#ifdef USE_LIBXSLT

/* libxml includes */

#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

/* libxslt includes */

#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/security.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#endif /* USE_LIBXSLT */


/* externally accessible functions */

Datum		xslt_process(PG_FUNCTION_ARGS);

#ifdef USE_LIBXSLT

/* declarations to come from xpath.c */
extern PgXmlErrorContext *pgxml_parser_init(PgXmlStrictness strictness);

/* local defs */
static const char **parse_params(text *paramstr);
#endif   /* USE_LIBXSLT */


PG_FUNCTION_INFO_V1(xslt_process);

Datum
xslt_process(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXSLT

	text	   *doct = PG_GETARG_TEXT_P(0);
	text	   *ssheet = PG_GETARG_TEXT_P(1);
	text	   *result;
	text	   *paramstr;
	const char **params;
<<<<<<< HEAD
	xsltStylesheetPtr stylesheet = NULL;
	xmlDocPtr	doctree;
	xmlDocPtr	restree;
	xmlDocPtr	ssdoc;
	xsltSecurityPrefsPtr xslt_sec_prefs;
	bool		xslt_sec_prefs_error;
	xsltTransformContextPtr xslt_ctxt;
	xmlChar    *resstr;
	int			resstat;
	int			reslen;
=======
	PgXmlErrorContext *xmlerrcxt;
	volatile xsltStylesheetPtr stylesheet = NULL;
	volatile xmlDocPtr doctree = NULL;
	volatile xmlDocPtr restree = NULL;
	volatile xmlDocPtr ssdoc = NULL;
	volatile int resstat = -1;
	xmlChar    *resstr = NULL;
	int			reslen = 0;
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56

	if (fcinfo->nargs == 3)
	{
		paramstr = PG_GETARG_TEXT_P(2);
		params = parse_params(paramstr);
	}
	else
	{
		/* No parameters */
		params = (const char **) palloc(sizeof(char *));
		params[0] = NULL;
	}

	/* Setup parser */
	xmlerrcxt = pgxml_parser_init(PG_XML_STRICTNESS_LEGACY);

<<<<<<< HEAD
	/* Parse document */
	doctree = xmlParseMemory((char *) VARDATA(doct),
							 VARSIZE(doct) - VARHDRSZ);

	if (doctree == NULL)
		xml_ereport(ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
					"error parsing XML document");

	/* Same for stylesheet */
	ssdoc = xmlParseMemory((char *) VARDATA(ssheet),
						   VARSIZE(ssheet) - VARHDRSZ);

	if (ssdoc == NULL)
	{
		xmlFreeDoc(doctree);
		xml_ereport(ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
					"error parsing stylesheet as XML document");
	}

	/* After this call we need not free ssdoc separately */
	stylesheet = xsltParseStylesheetDoc(ssdoc);

	if (stylesheet == NULL)
	{
		xmlFreeDoc(doctree);
=======
	PG_TRY();
	{
		/* Check to see if document is a file or a literal */

		if (VARDATA(doct)[0] == '<')
			doctree = xmlParseMemory((char *) VARDATA(doct), VARSIZE(doct) - VARHDRSZ);
		else
			doctree = xmlParseFile(text_to_cstring(doct));

		if (doctree == NULL)
			xml_ereport(xmlerrcxt, ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
						"error parsing XML document");

		/* Same for stylesheet */
		if (VARDATA(ssheet)[0] == '<')
		{
			ssdoc = xmlParseMemory((char *) VARDATA(ssheet),
								   VARSIZE(ssheet) - VARHDRSZ);
			if (ssdoc == NULL)
				xml_ereport(xmlerrcxt, ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
							"error parsing stylesheet as XML document");

			stylesheet = xsltParseStylesheetDoc(ssdoc);
		}
		else
			stylesheet = xsltParseStylesheetFile((xmlChar *) text_to_cstring(ssheet));

		if (stylesheet == NULL)
			xml_ereport(xmlerrcxt, ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
						"failed to parse stylesheet");

		restree = xsltApplyStylesheet(stylesheet, doctree, params);

		if (restree == NULL)
			xml_ereport(xmlerrcxt, ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
						"failed to apply stylesheet");

		resstat = xsltSaveResultToString(&resstr, &reslen, restree, stylesheet);
	}
	PG_CATCH();
	{
		if (stylesheet != NULL)
			xsltFreeStylesheet(stylesheet);
		if (restree != NULL)
			xmlFreeDoc(restree);
		if (doctree != NULL)
			xmlFreeDoc(doctree);
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
		xsltCleanupGlobals();

		pg_xml_done(xmlerrcxt, true);

		PG_RE_THROW();
	}
<<<<<<< HEAD

	xslt_ctxt = xsltNewTransformContext(stylesheet, doctree);

	xslt_sec_prefs_error = false;
	if ((xslt_sec_prefs = xsltNewSecurityPrefs()) == NULL)
		xslt_sec_prefs_error = true;

	if (xsltSetSecurityPrefs(xslt_sec_prefs, XSLT_SECPREF_READ_FILE,
							 xsltSecurityForbid) != 0)
		xslt_sec_prefs_error = true;
	if (xsltSetSecurityPrefs(xslt_sec_prefs, XSLT_SECPREF_WRITE_FILE,
							 xsltSecurityForbid) != 0)
		xslt_sec_prefs_error = true;
	if (xsltSetSecurityPrefs(xslt_sec_prefs, XSLT_SECPREF_CREATE_DIRECTORY,
							 xsltSecurityForbid) != 0)
		xslt_sec_prefs_error = true;
	if (xsltSetSecurityPrefs(xslt_sec_prefs, XSLT_SECPREF_READ_NETWORK,
							 xsltSecurityForbid) != 0)
		xslt_sec_prefs_error = true;
	if (xsltSetSecurityPrefs(xslt_sec_prefs, XSLT_SECPREF_WRITE_NETWORK,
							 xsltSecurityForbid) != 0)
		xslt_sec_prefs_error = true;
	if (xsltSetCtxtSecurityPrefs(xslt_sec_prefs, xslt_ctxt) != 0)
		xslt_sec_prefs_error = true;

	if (xslt_sec_prefs_error)
	{
		xsltFreeStylesheet(stylesheet);
		xmlFreeDoc(doctree);
		xsltFreeSecurityPrefs(xslt_sec_prefs);
		xsltFreeTransformContext(xslt_ctxt);
		xsltCleanupGlobals();
		xml_ereport(ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
					"could not set libxslt security preferences");
	}

	restree = xsltApplyStylesheetUser(stylesheet, doctree, params,
									  NULL, NULL, xslt_ctxt);

	if (restree == NULL)
	{
		xsltFreeStylesheet(stylesheet);
		xmlFreeDoc(doctree);
		xsltFreeSecurityPrefs(xslt_sec_prefs);
		xsltFreeTransformContext(xslt_ctxt);
		xsltCleanupGlobals();
		xml_ereport(ERROR, ERRCODE_EXTERNAL_ROUTINE_EXCEPTION,
					"failed to apply stylesheet");
	}

	resstat = xsltSaveResultToString(&resstr, &reslen, restree, stylesheet);
=======
	PG_END_TRY();
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56

	xsltFreeStylesheet(stylesheet);
	xmlFreeDoc(restree);
	xmlFreeDoc(doctree);
<<<<<<< HEAD
	xsltFreeSecurityPrefs(xslt_sec_prefs);
	xsltFreeTransformContext(xslt_ctxt);

	xsltCleanupGlobals();

=======
	xsltCleanupGlobals();

	pg_xml_done(xmlerrcxt, false);

>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56
	/* XXX this is pretty dubious, really ought to throw error instead */
	if (resstat < 0)
		PG_RETURN_NULL();

	result = cstring_to_text_with_len((char *) resstr, reslen);

	if (resstr)
		xmlFree(resstr);

	PG_RETURN_TEXT_P(result);
<<<<<<< HEAD

#else /* !USE_LIBXSLT */
=======
#else							/* !USE_LIBXSLT */
>>>>>>> 80edfd76591fdb9beec061de3c05ef4e9d96ce56

	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("xslt_process() is not available without libxslt")));
	PG_RETURN_NULL();
#endif   /* USE_LIBXSLT */
}

#ifdef USE_LIBXSLT

static const char **
parse_params(text *paramstr)
{
	char	   *pos;
	char	   *pstr;
	char	   *nvsep = "=";
	char	   *itsep = ",";
	const char **params;
	int			max_params;
	int			nparams;

	pstr = text_to_cstring(paramstr);

	max_params = 20;			/* must be even! */
	params = (const char **) palloc((max_params + 1) * sizeof(char *));
	nparams = 0;

	pos = pstr;

	while (*pos != '\0')
	{
		if (nparams >= max_params)
		{
			max_params *= 2;
			params = (const char **) repalloc(params,
										  (max_params + 1) * sizeof(char *));
		}
		params[nparams++] = pos;
		pos = strstr(pos, nvsep);
		if (pos != NULL)
		{
			*pos = '\0';
			pos++;
		}
		else
		{
			/* No equal sign, so ignore this "parameter" */
			nparams--;
			break;
		}

		/* since max_params is even, we still have nparams < max_params */
		params[nparams++] = pos;
		pos = strstr(pos, itsep);
		if (pos != NULL)
		{
			*pos = '\0';
			pos++;
		}
		else
			break;
	}

	/* Add the terminator marker; we left room for it in the palloc's */
	params[nparams] = NULL;

	return params;
}

#endif   /* USE_LIBXSLT */
