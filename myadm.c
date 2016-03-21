/* See LICENSE file for copyright and license details.
 *
 * myadm is a text-based TUI for MySQL. It emulates the mutt interface through
 * the STFL library and talk with the SQL server using libmysqlclient.
 *
 * Each piece of information displayed is called an item. Items are organized
 * in a linked items list on each view. A view contains an STFL form where all
 * graphical elements are drawn along with all related informations. Each item
 * contains a bit array to indicate tags of an item.
 *
 * To understand everything else, start reading main().
*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <mysql.h>
#include <stfl.h>
#include <langinfo.h>
#include <locale.h>
#include <curses.h> /* For curs_set() */

#include "arg.h"
char *argv0;

#define LENGTH(X)               (sizeof X / sizeof X[0])
#define QUOTE(S)		(stfl_ipool_fromwc(ipool, stfl_quote(stfl_ipool_towc(ipool, S))))
#define LINESIZE(N)		(MAXCOLSZ * (N) + fldseplen * ((N) - 1) + 1);

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Item Item;
struct Item {
	char **cols;
	int *lens;
	int ncols;
	unsigned int flags;
	Item *next;
};

typedef struct Field Field;
struct Field {
	char name[64]; /* MySQL max field name is 64 bytes */
	int len;
	Field *next;
};

typedef struct {
	const char *mode;
	const wchar_t *modkey;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	char *name;
	void (*func)(const Arg *arg);
} Mode;

typedef struct View View;
struct View {
	Mode *mode;
	Item *items;
	Item *choice;
	Field *fields;
	int nitems;
	int nfields;
	struct stfl_form *form;
	View *next;
};

/* function declarations */
void apply(const Arg *arg);
void attach(View *v);
void attachfieldto(Field *f, Field **ff);
void attachitemto(Item *i, Item **ii);
char ask(const char *msg, char *opts);
void cleanup(void);
void cleanupfields(Field **fields);
void cleanupitems(Item **items);
void cleanupview(View *v);
Item *cloneitem(Item *item);
void databases(const Arg *arg);
void detach(View *v);
void detachfieldfrom(Field *f, Field **ff);
void detachitemfrom(Item *i, Item **ii);
void die(const char *errstr, ...);
void *ecalloc(size_t nmemb, size_t size);
void flagas(const Arg *arg);
Item *getitem(void);
int *getmaxlengths(View *view);
void help(const Arg *arg);
void itempos(const Arg *arg);
MYSQL_RES *mysql_exec(const char *sqlstr, ...);
int mysql_fields(MYSQL_RES *res, Field **fields);
int mysql_items(MYSQL_RES *res, Item **items);
void mysql_listview(MYSQL_RES *res, int showfds);
View *newaview(const char *name, void (*func)(const Arg *arg));
void stfl_showfields(Field *fds, int *lens);
void stfl_showitems(Item *items, int *lens);
void quit(const Arg *arg);
void records(const Arg *arg);
void reload(const Arg *arg);
void run(void);
void setup(void);
void sigint_handler(int sig);
void stfl_setf(const char *name, const char *fmtstr, ...);
void stfl_putitem(Item *item, int *lens);
int stripesc(char *src, char *dst, int len);
void tables(const Arg *arg);
void viewprev(const Arg *arg);

#include "config.h"

/* variables */
static int running = 1;
static MYSQL *mysql;
static View *views, *selview = NULL;
static struct stfl_ipool *ipool;
static int fldseplen;

/* function implementations */
void
apply(const Arg *arg) {
	/* XXX if no pending changes, notice and returns */
	if(arg->i) {
		char *opts = "yn";
		if(ask("Apply changes ([y]/n)?", opts) != opts[0])
			return;
	}
	/* XXX ... */
	stfl_setf("status", "Changes applied.");
}

void
attach(View *v) {
	v->next = views;
	views = v;
}

void
attachfieldto(Field *f, Field **ff) {
	Field **l;

	for(l = ff; *l && (*l)->next; l = &(*l)->next);
	if(!*l)
		*l = f;
	else
		(*l)->next = f;
}

void
attachitemto(Item *i, Item **ii) {
	Item **l;

	for(l = ii; *l && (*l)->next; l = &(*l)->next);
	if(!*l)
		*l = i;
	else
		(*l)->next = i;
}

char
ask(const char *msg, char *opts) {
	char *o, c;

	stfl_setf("status", msg);
	stfl_run(selview->form, -1);
	while((c = getchar())) {
		if(c == '\r') {
			o = &opts[0];
			break;
		}
		for(o = opts; *o; ++o)
			if(c == *o)
				break;
		if(*o)
			break;
	}
	stfl_setf("status", "");
	return *o;
}

void
cleanup(void) {
	while(views)
		cleanupview(views);
	stfl_reset();
	stfl_ipool_destroy(ipool);
	mysql_close(mysql);
}

void
cleanupview(View *v) {
	detach(v);
	cleanupitems(&v->items);
	cleanupfields(&v->fields);
	if(v->form)
		stfl_free(v->form);
	if(v->choice)
		cleanupitems(&v->choice);
	free(v);
}

void
cleanupfields(Field **fields) {
	Field *f;

	while(*fields) {
		f = *fields;
		detachfieldfrom(f, fields);
		free(f);
	}
}

void
cleanupitems(Item **items) {
	Item *i;

	while(*items) {
		i = *items;
		detachitemfrom(i, items);
		while(--i->ncols >= 0)
			free(i->cols[i->ncols]);
		free(i->lens);
		free(i->cols);
		free(i);
	}
}

Item *
cloneitem(Item *item) {
	Item *ic;
	int i, nfds;

	if(!item)
		return NULL;

	nfds = item->ncols;
	ic = ecalloc(1, sizeof(Item));
	ic->cols = ecalloc(nfds, sizeof(char *));
	ic->lens = ecalloc(nfds, sizeof(int));
	ic->ncols = nfds;
	ic->flags = item->flags;
	for(i = 0; i < nfds; ++i) {
		ic->cols[i] = ecalloc(item->lens[i], sizeof(char));
		ic->lens[i] = item->lens[i];
		memcpy(ic->cols[i], item->cols[i], item->lens[i]);
	}
	return ic;
}

void
databases(const Arg *arg) {
	int refresh = (selview && !strcmp(selview->mode->name, "databases"));
	MYSQL_RES *res;

	if(!(res = mysql_exec("show databases")))
		die("databases\n");
	if(!refresh)
		selview = newaview("databases", databases);
	mysql_listview(res, 0);
	mysql_free_result(res);
	stfl_setf("title", "Databases in `%s`", dbhost);
	stfl_setf("info", "%d DB(s)", selview->nitems);
}

void
detach(View *v) {
	View **tv;

	for(tv = &views; *tv && *tv != v; tv = &(*tv)->next);
	*tv = v->next;
}

void
detachfieldfrom(Field *f, Field **ff) {
	Field **tf;

	for(tf = &(*ff); *tf && *tf != f; tf = &(*tf)->next);
	*tf = f->next;
}

void
detachitemfrom(Item *i, Item **ii) {
	Item **ti;

	for(ti = &(*ii); *ti && *ti != i; ti = &(*ti)->next);
	*ti = i->next;
}

void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("Cannot allocate memory.");
	return p;
}

void
flagas(const Arg *arg) {
}

Item *
getitem(void) {
	const char *spos = stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos"));
	Item *item;
	int pos, n;

	if(!(selview && spos))
		return NULL;
	pos = atoi(spos);

	for(item = selview->items, n = 0; item; item = item->next, ++n)
		if(n == pos)
			break;
	return item;
}

int *
getmaxlengths(View *view) {
	Item *item;
	Field *fld;
	int i, nfds, *lens;

	if(view->nfields)
		nfds = view->nfields;
	else if(view->items)
		nfds = view->items->ncols;
	else
		return NULL;

	lens = ecalloc(nfds, sizeof(int));
	for(fld = view->fields, i = 0; fld; fld = fld->next, ++i)
		lens[i] = (fld->len <= MAXCOLSZ ? fld->len : MAXCOLSZ);
	for(item = view->items; item; item = item->next)
		for(i = 0; i < item->ncols; ++i)
			if(lens[i] < item->lens[i])
				lens[i] = (item->lens[i] <= MAXCOLSZ ? item->lens[i] : MAXCOLSZ);
	return lens;
}

void
help(const Arg *arg) {
	int refresh = (selview && !strcmp(selview->mode->name, "help"));

	if(!refresh)
		selview = newaview("help", help);
	else
		stfl_free(selview->form);
	selview->form = stfl_create(L"<help.stfl>");
}

void
itempos(const Arg *arg) {
	const char *spos = stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos"));
	char tmp[8];
	int pos;

	if(!spos)
		return;

	pos = atoi(spos) + arg->i;
	if(pos < 0)
		pos = 0;
	else if(pos >= selview->nitems)
		pos = selview->nitems - 1;

	snprintf(tmp, sizeof tmp, "%d", pos);
	stfl_set(selview->form, L"pos", stfl_ipool_towc(ipool, tmp));
}

MYSQL_RES *
mysql_exec(const char *sqlstr, ...) {
	MYSQL_RES *res;
	va_list ap;
	char sql[128];
	int sqlen;

	va_start(ap, sqlstr);
	sqlen = vsnprintf(sql, sizeof sql, sqlstr, ap);
	va_end(ap);

	if(mysql_real_query(mysql, sql, sqlen))
		return NULL;
	res = mysql_store_result(mysql);
	if(!res)
		return NULL;
	return res;
}

int
mysql_fields(MYSQL_RES *res, Field **fields) {
	MYSQL_FIELD *fds;
	Field *field;
	int nfds, i;

	nfds = mysql_num_fields(res);
	if(!nfds)
		return 0;
	fds = mysql_fetch_fields(res);
	for(i = 0; i < nfds; ++i) {
		field = ecalloc(1, sizeof(Field));
		field->len = fds[i].name_length;
		memcpy(field->name, fds[i].name, field->len);
		attachfieldto(field, fields);
	}
	return nfds;
}

int
mysql_items(MYSQL_RES *res, Item **items) {
	MYSQL_ROW row;
	Item *item;
	int i, nfds, nrows;
	unsigned long *lens;

	nfds = mysql_num_fields(res);
	nrows = mysql_num_rows(res);

	*items = NULL;
	while((row = mysql_fetch_row(res))) {
		item = ecalloc(1, sizeof(Item));
		item->lens = ecalloc(nfds, sizeof(int));
		item->cols = ecalloc(nfds, sizeof(char *));
		lens = mysql_fetch_lengths(res);
		item->ncols = nfds;
		for(i = 0; i < nfds; ++i) {
			item->cols[i] = ecalloc(lens[i], sizeof(char));
			memcpy(item->cols[i], row[i], lens[i]);
			item->lens[i] = lens[i];
		}
		attachitemto(item, items);
	}
	return nrows;
}

void
mysql_listview(MYSQL_RES *res, int showfds) {
	int *lens;

	cleanupitems(&selview->items);
	selview->nitems = mysql_items(res, &selview->items);

	if(!selview->form) {
		selview->form = stfl_create(L"<items.stfl>");
		stfl_run(selview->form, -1); /* refresh ncurses */
		curs_set(0);
	}

	if(showfds) {
		cleanupfields(&selview->fields);
		selview->nfields = mysql_fields(res, &selview->fields);
		lens = getmaxlengths(selview);
		stfl_showfields(selview->fields, lens);
	}
	else {
		lens = getmaxlengths(selview);
	}

	stfl_showitems(selview->items, lens);
	free(lens);
}

View *
newaview(const char *name, void (*func)(const Arg *arg)) {
	View *v;

	v = ecalloc(1, sizeof(View));
	v->mode = ecalloc(1, sizeof(Mode));
	v->mode->name = ecalloc(strlen(name)+1, sizeof(char));
	strcpy(v->mode->name, name);
	v->mode->func = func;
	attach(v);
	return v;
}

void
stfl_showfields(Field *fds, int *lens) {
	Field *fld;
	char line[COLS+1], txt[MAXCOLSZ+1], col[MAXCOLSZ+1];
	int i, len, nfields, linesz;

	if(!(fds && lens))
		return;

	for(fld = fds, nfields = 0; fld; fld = fld->next, ++nfields);

	linesz = sizeof line;
	line[0] = '\0';
	for(fld = fds, i = 0; fld; fld = fld->next, ++i) {
		if(i) {
			strncat(line, FLDSEP, linesz);
			linesz -= fldseplen;
			if(linesz <= 0)
				break;
		}
		len = stripesc(col, fld->name, lens[i]);
		col[len] = '\0';
		snprintf(txt, sizeof txt, "%-*.*s", lens[i], lens[i], col);
		strncat(line, txt, linesz);
		linesz -= lens[i];
		if(linesz <= 0)
			break;
	}

	stfl_setf("subtle", "%s", line);
	stfl_setf("showsubtle", (line[0] ? "1" : "0"));
}

void
stfl_showitems(Item *items, int *lens) {
	Item *item;

	stfl_modify(selview->form, L"items", L"replace_inner", L"vbox"); /* clear */
	for(item = selview->items; item; item = item->next)
		stfl_putitem(item, lens);
	stfl_set(selview->form, L"pos", L"0");
}

/* XXX Improved logic:
 * -1 only ask if there are pending changes
 *  1 always ask 
 *  0 never ask */
void
quit(const Arg *arg) {
	if(arg->i) {
		char *opts = "yn";
		if(ask("Do you want to quit ([y]/n)?", opts) != opts[0])
			return;
	}
	running = 0;
}

void
records(const Arg *arg) {
	int refresh = (selview && !strcmp(selview->mode->name, "records"));
	Item *choice = (refresh ? selview->choice : cloneitem(getitem()));
	MYSQL_RES *res;
	char *tbl;

	choice = (refresh ? selview->choice : cloneitem(getitem()));
	if(!choice->ncols)
		die("records: no choice.\n");
	tbl = calloc(choice->lens[0] + 1, sizeof(char));
	memcpy(tbl, choice->cols[0], choice->lens[0]);
	if(!(res = mysql_exec("select * from `%s`", tbl)))
		die("records: cannot select `%s`\n", tbl);
	if(!refresh) {
		selview = newaview("records", records);
		selview->choice = choice;
	}
	mysql_listview(res, 1);
	mysql_free_result(res);
	stfl_setf("title", "Records in `%s`", tbl);
	stfl_setf("info", "---Core: %d record(s)", selview->nitems);
	free(tbl);
}

void
reload(const Arg *arg) {
	const wchar_t *pos = stfl_get(selview->form, L"pos");
	if(!selview->mode->func)
		return;
	selview->mode->func(NULL);
	if(pos)
		stfl_set(selview->form, L"pos", pos);
}

void
run(void) {
	Key *k;
	const wchar_t *ev;
	unsigned int i;

	while(running) {
		if(!(ev = stfl_run(selview->form, 0)))
			continue;
		stfl_setf("status", "");
		k = NULL;
		for(i = 0; i < LENGTH(keys); ++i)
			if(!((keys[i].mode && strcmp(selview->mode->name, keys[i].mode))
			|| wcscmp(ev, keys[i].modkey)))
				k = &keys[i];
		if(k)
			k->func(&k->arg);
	}
}

void
setup(void) {
	struct sigaction sa;

	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, dbhost, dbuser, dbpass, NULL, 0, NULL, 0) == NULL)
		die("Cannot connect to the database.\n");

	fldseplen = strlen(FLDSEP);
	ipool = stfl_ipool_create(nl_langinfo(CODESET));
	welcome(NULL);
	stfl_setf("status", "Welcome to %s-%s", __FILE__, VERSION);

	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
}

void
sigint_handler(int sig) {
	Arg arg = {.i = 1};
	quit(&arg);
}

void
stfl_setf(const char *name, const char *fmtstr, ...) {
	va_list ap;
	char s[256];

	va_start(ap, fmtstr);
	vsnprintf(s, sizeof s, fmtstr, ap);
	va_end(ap);
	stfl_set(selview->form, stfl_ipool_towc(ipool, name), stfl_ipool_towc(ipool, s));
}

void
stfl_putitem(Item *item, int *lens) {
	const char *qline;
	char *stfl, line[COLS + 1], txt[MAXCOLSZ+1], col[MAXCOLSZ+1];
	int i, len, linesz;

	if(!(item && lens))
		return;

	linesz = sizeof line;
	line[0] = '\0';
	for(i = 0; i < item->ncols; ++i) {
		if(i) {
			strncat(line, FLDSEP, linesz);
			linesz -= fldseplen;
			if(linesz <= 0)
				break;
		}
		len = stripesc(col, item->cols[i], lens[i]);
		col[len] = '\0';
		snprintf(txt, sizeof txt, "%-*.*s", lens[i], lens[i], col);
		strncat(line, txt, linesz);
		linesz -= lens[i];
		if(linesz <= 0)
			break;
	}

	/* XXX cleanup */
	qline = QUOTE(line);
	i = strlen("listitem text:");
	len = strlen(qline) + i;
	stfl = ecalloc(len + 1, sizeof(char));
	memcpy(stfl, "listitem text:", i);
	memcpy(&stfl[i], qline, len - i - 1);
	stfl_modify(selview->form, L"items", L"append", stfl_ipool_towc(ipool, stfl));

	free(stfl);
}

int
stripesc(char *dst, char *src, int len) {
	int i, n;

	for(i = 0, n = 0; i < len; ++i)
		if(src[i] != '\r' && src[i] != '\n' && src[i] != '\t')
			dst[n++] = src[i];
	return n;
}

void
usage(void) {
	die("Usage: %s [-vhup <arg>]\n", argv0);
}

void
tables(const Arg *arg) {
	int refresh = (selview && !strcmp(selview->mode->name, "tables"));
	Item *choice = (refresh ? selview->choice : cloneitem(getitem()));
	MYSQL_RES *res;

	if(!refresh) {
		selview = newaview("tables", tables);
		selview->choice = choice;
		mysql_select_db(mysql, choice->cols[0]);
	}
	if(!(res = mysql_exec("show tables")))
		die("tables\n");
	mysql_listview(res, 0);
	mysql_free_result(res);
	stfl_setf("title", "Tables in `%s`", choice->cols[0]);
	stfl_setf("info", "%d table(s)", selview->nitems);
}

void
viewprev(const Arg *arg) {
	View *v;

	if(!selview->next)
		return;
	v = selview->next;
	cleanupview(selview);
	selview = v;
}

int
main(int argc, char **argv) {
	ARGBEGIN {
	case 'h':
		dbhost = EARGF(usage());
		break;
	case 'u':
		dbuser = EARGF(usage());
		break;
	case 'p':
		dbpass = EARGF(usage());
		break;
	case 'v':
		die("%s " VERSION " (c) 2016 Claudio Alessi\n", argv0);
	} ARGEND;

	setup();
	run();
	cleanup();
	return 0;
}
