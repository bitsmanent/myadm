/* See LICENSE file for copyright and license details. */
/*
 * An experiment which is working surprisingly well.
 *
 * Relevant docs:
 * http://svn.clifford.at/stfl/trunk/README
 * http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
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

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Item Item;
struct Item {
	char **fields;
	int nfields;
	unsigned int flags;
	Item *next;
};

typedef struct {
	const char *mode;
	const wchar_t *modkey;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *name;
	void (*func)(void);
} Mode;

typedef struct View View;
struct View {
	Mode *mode;
	Item *items;
	Item *choice;
	int nitems;
	struct stfl_form *form;
	View *next;
};

/* function declarations */
void apply(const Arg *arg);
void attach(View *v);
void attachitemto(Item *i, Item **ii);
char choose(const char *msg, char *opts);
void cleanup(void);
void cleanupview(View *v);
void cleanupitems(Item *i);
Item *cloneitem(Item *item);
void databases(void);
void detach(View *v);
void detachitemfrom(Item *i, Item **ii);
void die(const char *errstr, ...);
void *ecalloc(size_t nmemb, size_t size);
void flagas(const Arg *arg);
Item *getitem(void);
void itempos(const Arg *arg);
MYSQL_RES *mysql_exec(const char *sqlstr, ...);
int mysql_items(MYSQL_RES *res, Item **items);
void mysql_listview(MYSQL_RES *res);
void mysql_showfields(MYSQL_RES *res);
void quit(const Arg *arg);
void records(void);
void reload(const Arg *arg);
void run(void);
void setmode(const Arg *arg);
void setup(void);
void sigint_handler(int sig);
void stfl_setf(const char *name, const char *fmtstr, ...);
void stfl_putitem(Item *item, unsigned long *lens);
void tables(void);
void text(void);
void usedb(const Arg *arg);
void viewprev(const Arg *arg);

#include "config.h"

/* variables */
static int running = 1;
static MYSQL *mysql;
static View *views, *selview;
static struct stfl_ipool *ipool;

/* function implementations */
void
apply(const Arg *arg) {
	/* XXX if no pending changes, notice and returns */
	if(arg->i) {
		char *opts = "yn";
		if(choose("Apply changes ([y]/n)?", opts) != opts[0])
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
attachitemto(Item *i, Item **ii) {
	Item **l;

	for(l = ii; *l && (*l)->next; l = &(*l)->next);
	if(!*l)
		*l = i;
	else
		(*l)->next = i;
}

char
choose(const char *msg, char *opts) {
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
cleanupitems(Item *i) {
	while(i && i->next) {
		detachitemfrom(i, &i);
		while(--i->nfields >= 0)
			free(i->fields[i->nfields]);
		free(i->fields);
		free(i);
	}
}

void
cleanupview(View *v) {
	detach(v);
	cleanupitems(v->items);
	if(v->form)
		stfl_free(v->form);
	free(v->choice);
	free(v);
}

Item *
cloneitem(Item *item) {
	Item *ic;
	int i;

	if(!item)
		return NULL;
	ic = ecalloc(1, sizeof(Item));
	ic->nfields = item->nfields;
	ic->flags = item->flags;
	ic->fields = ecalloc(item->nfields, sizeof(char *));
	for(i = 0; i < item->nfields; ++i) {
		ic->fields[i] = ecalloc(64, sizeof(char));
		snprintf(ic->fields[i], 64, "%s", item->fields[i]);
	}
	return ic;
}

void
databases(void) {
	MYSQL_RES *res;

	if(!(res = mysql_exec("show databases")))
		die("databases");
	mysql_listview(res);
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
	Item *item;
	int pos, n;

	if(!selview)
		return NULL;
	pos = atoi(stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos")));

	for(item = selview->items, n = 0; item; item = item->next, ++n)
		if(n == pos)
			break;
	return item;
}

void
itempos(const Arg *arg) {
	int pos = atoi(stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos")));
	char tmp[8];

	if(!selview->nitems)
		return;

	pos += arg->i;
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
	char sql[8096];

	va_start(ap, sqlstr);
	vsnprintf(sql, sizeof sql, sqlstr, ap);
	va_end(ap);

	if(mysql_real_query(mysql, sql, strlen(sql)))
		return NULL;
	res = mysql_store_result(mysql);
	if(!res)
		return NULL;
	return res;
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
		item->nfields = nfds;
		if(nfds) {
			lens = mysql_fetch_lengths(res);
			item->fields = ecalloc(nfds, sizeof(char *));
			for(i = 0; i < nfds; ++i) {
				++lens[i]; /* Add NUL-byte */
				item->fields[i] = ecalloc(lens[i], sizeof(char));
				snprintf(item->fields[i], lens[i], "%s", row[i]);
			}
		}
		attachitemto(item, &(*items));
	}
	return nrows;
}

void
mysql_listview(MYSQL_RES *res) {
	Item *item;
	unsigned long *lens;

	cleanupitems(selview->items);
	selview->nitems = mysql_items(res, &selview->items);
	if(!selview->form)
		selview->form = stfl_create(L"<items.stfl>");
	stfl_modify(selview->form, L"items", L"replace_inner", L"vbox"); /* clear */
	lens = mysql_fetch_lengths(res);
	for(item = selview->items; item; item = item->next)
		stfl_putitem(item, lens);
	stfl_set(selview->form, L"pos", L"0");
}

void
mysql_showfields(MYSQL_RES *res) {
	MYSQL_FIELD *fds;
	char txt[512];
	char t[32];
	unsigned long *lens;
	int i, len, slen;

	fds = mysql_fetch_fields(res);
	len = mysql_num_fields(res);
	lens = mysql_fetch_lengths(res);

	txt[0] = '\0';
	for(i = 0; i < len; ++i) {
		slen = (lens && lens[i] > 16 ? lens[i] : 16);
		if(i)
			strncat(txt, " | ", sizeof txt);
		snprintf(t, sizeof t, "%-*s", slen, fds[i].name);
		strncat(txt, t, sizeof txt);
	}
	stfl_setf("subtle", "%s", txt);
	stfl_setf("showsubtle", "1");
}

/* XXX Improved logic:
 * -1 only ask if there are pending changes
 *  1 always ask 
 *  0 never ask */
void
quit(const Arg *arg) {
	if(arg->i) {
		char *opts = "yn";
		if(choose("Do you want to quit ([y]/n)?", opts) != opts[0])
			return;
	}
	running = 0;
}

void
records(void) {
	MYSQL_RES *res;

	if(!(selview->choice && selview->choice->nfields))
		die("records: no choice.\n");
	if(!(res = mysql_exec("select * from `%s`", selview->choice->fields[0])))
		die("records\n");
	mysql_listview(res);
	mysql_showfields(res);
	mysql_free_result(res);
	stfl_setf("title", "Records in `%s`", selview->choice->fields[0]);
	stfl_setf("info", "---Core: %d record(s)", selview->nitems);
}

void
reload(const Arg *arg) {
	const wchar_t *pos = stfl_get(selview->form, L"pos");
	selview->mode->func();
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
setmode(const Arg *arg) {
	const Mode *m = (arg ? arg->v : &modes[0]);
	View *v;
	unsigned int i;

	if(selview && selview->mode && !strcmp(selview->mode->name, m->name))
		return;
	for(v = views; v; v = v->next)
		if(!strcmp(v->mode->name, m->name))
			break;
	if(!v) {
		v = ecalloc(1, sizeof(View));
		for(i = 0; i < LENGTH(modes); ++i)
			if(!strcmp(modes[i].name, m->name))
				v->mode = &modes[i];
		attach(v);
	}
	if(v->choice)
		free(v->choice);
	v->choice = cloneitem(getitem());
	selview = v;
	selview->mode->func();

	stfl_run(selview->form, -1);
	curs_set(0);
}

void
setup(void) {
	struct sigaction sa;

	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, dbhost, dbuser, dbpass, NULL, 0, NULL, 0) == NULL)
		die("Cannot connect to the database.\n");

	ipool = stfl_ipool_create(nl_langinfo(CODESET));
	setmode(NULL);
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
	char s[512];

	va_start(ap, fmtstr);
	vsnprintf(s, sizeof s, fmtstr, ap);
	va_end(ap);
	stfl_set(selview->form, stfl_ipool_towc(ipool, name), stfl_ipool_towc(ipool, s));
}

void
stfl_putitem(Item *item, unsigned long *lens) {
	char t[32];
	char txt[512];
	char itm[128];
	int i, slen;

	itm[0] = '\0';
	for(i = 0; i < item->nfields; ++i) {
		slen = (lens && lens[i] > 16 ? lens[i] : 16);
		snprintf(t, sizeof t, "%-*.16s", slen, item->fields[i]);
		if(i)
			strncat(itm, " | ", sizeof itm);
		strncat(itm, t, sizeof itm);
	}

	snprintf(txt, sizeof txt, "listitem text:%s", QUOTE(itm));
	stfl_modify(selview->form, L"items", L"append", stfl_ipool_towc(ipool, txt));
}

void
tables(void) {
	MYSQL_RES *res;

	if(!(res = mysql_exec("show tables")))
		die("tables\n");
	mysql_listview(res);
	mysql_free_result(res);
	stfl_setf("title", "Tables in `%s`", selview->choice->fields[0]);
	stfl_setf("info", "%d table(s)", selview->nitems);
}

void
text(void) {
}

void
usage(void) {
	die("Usage: %s [-hup <arg>]\n", argv0);
}

void
usedb(const Arg *arg) {
	Item *item = getitem();
	mysql_select_db(mysql, item->fields[0]);
	setmode(arg);
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
	} ARGEND;

	setup();
	run();
	cleanup();
	return 0;
}
