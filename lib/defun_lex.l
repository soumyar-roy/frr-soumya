/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * clippy (CLI preparator in python) C pseudo-lexer
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

/* This is just enough of a lexer to make rough sense of a C source file.
 * It handles C preprocessor directives, strings, and looks for FRR-specific
 * idioms (aka DEFUN).
 *
 * There is some preliminary support for documentation comments for DEFUNs.
 * They would look like this (note the ~):  (replace \ by /)
 *
 * \*~  documentation for foobar_cmd
 *  *   parameter does xyz
 *  *\
 * DEFUN(foobar_cmd, ...)
 *
 * This is intended for user documentation / command reference.  Don't put
 * code documentation in it.
 */

%top{
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
}
%{
/* ignore harmless bugs in old versions of flex */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

#include "config.h"
#include <Python.h>
#include <string.h>
#include <stdlib.h>

#include "command_graph.h"
#include "clippy.h"

#define ID		258
#define PREPROC		259
#define OPERATOR	260
#define STRING		261
#define COMMENT		262
#define SPECIAL		263

#define DEFUNNY		270
#define INSTALL		271
#define AUXILIARY	272

int comment_link;
char string_end;

char *value;
static const char *yyfilename;

static void extendbuf(char **what, const char *arg)
{
	if (!*what)
		*what = strdup(arg);
	else {
		size_t vall = strlen(*what), argl = strlen(arg);
		*what = realloc(*what, vall + argl + 1);
		memcpy(*what + vall, arg, argl);
		(*what)[vall + argl] = '\0';
	}
}
#define extend(x) extendbuf(&value, x)

#ifndef __clang_analyzer__

%}

ID		[A-Za-z0-9_]+
OPERATOR	[!%&/\[\]{}=?:^|\*.;><~'\\+-]
SPECIAL		[(),]

%pointer
%option yylineno
%option noyywrap
%option noinput
%option nounput
%option outfile="lib/defun_lex.c"
%option prefix="def_yy"
%option 8bit

%s linestart
%x comment
%x linecomment
%x preproc
%x rstring
%%
				BEGIN(linestart);

\n				BEGIN(linestart);

<INITIAL,linestart,preproc>"/*"	comment_link = YY_START; extend(yytext); BEGIN(comment);
<comment>[^*\n]*		extend(yytext);
<comment>"*"+[^*/\n]*		extend(yytext);
<comment>\n			extend(yytext);
<comment>"*"+"/"		extend(yytext); BEGIN(comment_link); return COMMENT;

<INITIAL,linestart,preproc>"//"	comment_link = YY_START; extend(yytext); BEGIN(linecomment);
<linecomment>[^\n]*		extend(yytext);
<linecomment>\n			BEGIN((comment_link == INITIAL) ? linestart : comment_link); return COMMENT;

<linestart>#			BEGIN(preproc);
<preproc>\n			BEGIN(INITIAL); return PREPROC;
<preproc>[^\n\\]+		extend(yytext);
<preproc>\\\n			extend(yytext);
<preproc>\\+[^\n]		extend(yytext);

[\"\']				string_end = yytext[0]; extend(yytext); BEGIN(rstring);
<rstring>[\"\']			{
					extend(yytext);
					if (yytext[0] == string_end) {
						BEGIN(INITIAL);
						return STRING;
					}
				}
<rstring>\\\n			/* ignore */
<rstring>\n			{
					fprintf(stderr,
						"%s:%d: string continues past the end of the line\n",
						yyfilename, yylineno);
					free(value);
					value = NULL;
					BEGIN(INITIAL);
					return STRING;
				}
<rstring>\\.			extend(yytext);
<rstring>[^\\\"\'\n]+		extend(yytext);

"DEFUN"				value = strdup(yytext); return DEFUNNY;
"DEFUN_NOSH"			value = strdup(yytext); return DEFUNNY;
"DEFUN_HIDDEN"			value = strdup(yytext); return DEFUNNY;
"DEFPY"				value = strdup(yytext); return DEFUNNY;
"DEFPY_NOSH"			value = strdup(yytext); return DEFUNNY;
"DEFPY_ATTR"			value = strdup(yytext); return DEFUNNY;
"DEFPY_HIDDEN"			value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG"			value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG_HIDDEN"		value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG_NOSH"		value = strdup(yytext); return DEFUNNY;
"ALIAS"				value = strdup(yytext); return DEFUNNY;
"ALIAS_HIDDEN"			value = strdup(yytext); return DEFUNNY;
"install_element"		value = strdup(yytext); return INSTALL;
"VTYSH_TARGETS"			value = strdup(yytext); return AUXILIARY;
"VTYSH_NODESWITCH"		value = strdup(yytext); return AUXILIARY;

[ \t\n]+			/* ignore */
\\				/* ignore */
{ID}				BEGIN(INITIAL); value = strdup(yytext); return ID;
{OPERATOR}			BEGIN(INITIAL); value = strdup(yytext); return OPERATOR;
{SPECIAL}			BEGIN(INITIAL); value = strdup(yytext); return SPECIAL;
.				/* printf("-- '%s' in init\n", yytext); */ BEGIN(INITIAL); return yytext[0];

%%

#else
extern int def_yylex(void);
extern int def_yylex_destroy(void);
#endif /* __clang_analyzer__ */

static int yylex_clr(char **retbuf)
{
	int rv = def_yylex();
	*retbuf = value;
	value = NULL;
	return rv;
}

static PyObject *get_args(const char *filename, int lineno)
{
	PyObject *pyObj = PyList_New(0);
	PyObject *pyArg = NULL;

	char *tval;
	int depth = 1;
	int token;

	while ((token = yylex_clr(&tval)) != YY_NULL) {
		if (token == SPECIAL && tval[0] == '(') {
			free(tval);
			break;
		}
		if (token == COMMENT) {
			free(tval);
			continue;
		}
		fprintf(stderr, "invalid input!\n");
		exit(1);
	}

	while ((token = yylex_clr(&tval)) != YY_NULL) {
		if (token == COMMENT) {
			free(tval);
			continue;
		}
		if (token == PREPROC) {
			free(tval);
			Py_DECREF(pyObj);
			return PyErr_Format(PyExc_ValueError,
					"%s:%d: cannot process CPP directive within argument list",
					filename, lineno);
		}
		if (token == SPECIAL) {
			if (depth == 1 && (tval[0] == ',' || tval[0] == ')')) {
				if (pyArg)
					PyList_Append(pyObj, pyArg);
				pyArg = NULL;
				if (tval[0] == ')') {
					free(tval);
					break;
				}
				free(tval);
				continue;
			}
			if (tval[0] == '(')
				depth++;
			if (tval[0] == ')')
				depth--;
		}
		if (!tval)
			return PyErr_Format(PyExc_ValueError,
					"%s:%d: invalid token in DEFPY parameters",
					filename, lineno);
		if (!pyArg)
			pyArg = PyList_New(0);
		PyList_Append(pyArg, PyUnicode_FromString(tval));
		free(tval);
	}
	return pyObj;
}

/* _clippy.parse() -- read a C file, returning a list of interesting bits.
 * note this ditches most of the actual C code. */
PyObject *clippy_parse(PyObject *self, PyObject *args)
{
	const char *filename;
	if (!PyArg_ParseTuple(args, "s", &filename))
		return NULL;
	
	FILE *fd = fopen(filename, "r");
	if (!fd)
		return PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);

	char *tval;
	int token;
	yyin = fd;
	value = NULL;
	yyfilename = filename;

	PyObject *pyCont = PyDict_New();
	PyObject *pyObj = PyList_New(0);
	PyDict_SetItemString(pyCont, "filename", PyUnicode_FromString(filename));
	PyDict_SetItemString(pyCont, "data", pyObj);

	while ((token = yylex_clr(&tval)) != YY_NULL) {
                int lineno = yylineno;
		PyObject *pyItem = NULL, *pyArgs;
		switch (token) {
		case DEFUNNY:
		case INSTALL:
		case AUXILIARY:
			pyArgs = get_args(filename, lineno);
			if (!pyArgs) {
				free(tval);
				Py_DECREF(pyCont);
				yyfilename = NULL;
				return NULL;
			}
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString(tval));
			PyDict_SetItemString(pyItem, "args", pyArgs);
			break;
		case COMMENT:
                        if (strncmp(tval, "//~", 3) && strncmp(tval, "/*~", 3))
                                break;
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString("COMMENT"));
			PyDict_SetItemString(pyItem, "line", PyUnicode_FromString(tval));
			break;
		case PREPROC:
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString("PREPROC"));
			PyDict_SetItemString(pyItem, "line", PyUnicode_FromString(tval));
			lineno--;
			break;
		}
		if (pyItem) {
			PyDict_SetItemString(pyItem, "lineno", PyLong_FromLong(lineno));
			PyList_Append(pyObj, pyItem);
		}
		free(tval);
	}
	def_yylex_destroy();
	fclose(fd);
	yyfilename = NULL;
	return pyCont;
}
