/* See LICENSE file for copyright and license details. */
/* http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <mysql.h>
#include <stfl.h>

#include <langinfo.h>
#include <locale.h>

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
void die(const char *errstr, ...);
void setup(void);
void cleanup(void);
void run(void);
void stfl_setf(const char *name, const char *fmtstr, ...);
void databases(void);
void tables(void);
void records(void);
void text(void);
MYSQL_RES *mysql_exec(const char *sqlstr, ...);
int mysql_items(MYSQL_RES *res, Item **items);
void mysql_listview(MYSQL_RES *res);
void attach(View *v);
void detach(View *v);
void attachitemto(Item *i, Item **ii);
void detachitemfrom(Item *i, Item **ii);
void *ecalloc(size_t nmemb, size_t size);
void cleanupview(View *v);
void cleanupitems(Item *i);
Item *getitem(void);
void stfl_putitem(Item *item);
char choose(const char *msg, char *opts);
Item *copyitem(Item *item);
void flagas(const Arg *arg);
void apply(const Arg *arg);
void quit(const Arg *arg);
void setmode(const Arg *arg);
void viewprev(const Arg *arg);
void usedb(const Arg *arg);
void usetable(const Arg *arg);
void userecord(const Arg *arg);
void itempos(const Arg *arg);
void reload(const Arg *arg);

#include "config.h"

/* variables */
static int running = 1;
static MYSQL *mysql;
static View *views, *selview;
static struct stfl_ipool *ipool;

/* function implementations */
char
choose(const char *msg, char *opts) {
	char *o, c;

	stfl_setf("statustext", msg);
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
	stfl_setf("statustext", "");
	return *o;
}

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
attach(View *v) {
	v->next = views;
	views = v;
}

void
detach(View *v) {
	View **tv;

	for(tv = &views; *tv && *tv != v; tv = &(*tv)->next);
	*tv = v->next;
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

void
detachitemfrom(Item *i, Item **ii) {
	Item **ti;

	for(ti = &(*ii); *ti && *ti != i; ti = &(*ti)->next);
	*ti = i->next;
}

Item *
copyitem(Item *item) {
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
	v->choice = copyitem(getitem());

	selview = v;
	selview->mode->func();

	stfl_setf("infotext", "---Core: %d items.", selview->nitems);
}

void
reload(const Arg *arg) {
	const wchar_t *pos = stfl_get(selview->form, L"pos");
	selview->mode->func();
	stfl_set(selview->form, L"pos", pos);
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

	nfds = mysql_num_fields(res);
	nrows = mysql_num_rows(res);

	*items = NULL;
	while((row = mysql_fetch_row(res))) {
		item = ecalloc(1, sizeof(Item));
		item->nfields = nfds;
		if(nfds) {
			item->fields = ecalloc(nfds, sizeof(char *));
			for(i = 0; i < nfds; ++i) {
				/* MySQL max column name length is 64 */ 
				item->fields[i] = ecalloc(64, sizeof(char));
				snprintf(item->fields[i], 64, "%s", row[i]);
			}
		}
		attachitemto(item, &(*items));
	}
	return nrows;
}

void
stfl_putitem(Item *item) {
	char t[32];
	char txt[256];
	char itm[128];
	int i;

	itm[0] = '\0';
	for(i = 0; i < item->nfields; ++i) {
		snprintf(t, sizeof t, "%-8.16s", item->fields[i]);
		if(i)
			strncat(itm, " | ", sizeof itm);
		strncat(itm, t, sizeof itm);
	}

	snprintf(txt, sizeof txt, "listitem text:%s", QUOTE(itm));
	stfl_modify(selview->form, L"items", L"append", stfl_ipool_towc(ipool, txt));
}

void
mysql_listview(MYSQL_RES *res) {
	Item *item;

	cleanupitems(selview->items);
	selview->nitems = mysql_items(res, &selview->items);
	if(!selview->form)
		selview->form = stfl_create(L"<items.stfl>");

	/* XXX columns row */

	stfl_modify(selview->form, L"items", L"replace_inner", L"vbox"); /* clear */
	if(selview->items)
		selview->form = stfl_create(L"<items.stfl>");

	for(item = selview->items; item; item = item->next)
		stfl_putitem(item);
}

void
databases(void) {
	MYSQL_RES *res;

	if(!(res = mysql_exec("show databases")))
		die("databases");
	mysql_listview(res);
	mysql_free_result(res);
}

void
tables(void) {
	MYSQL_RES *res;

	if(!(res = mysql_exec("show tables")))
		die("tables\n");
	mysql_listview(res);
	mysql_free_result(res);
}

void
records(void) {
	MYSQL_RES *res;

	if(!(selview->choice && selview->choice->nfields))
		die("records: no choice.\n");
	if(!(res = mysql_exec("select * from `%s`", selview->choice->fields[0])))
		die("records\n");
	mysql_listview(res);
	mysql_free_result(res);
}

void
text(void) {
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
usedb(const Arg *arg) {
	Item *item = getitem();
	mysql_select_db(mysql, item->fields[0]);
	setmode(arg);
}

void
usetable(const Arg *arg) {
	setmode(arg);
}

void
userecord(const Arg *arg) {
}

void
itempos(const Arg *arg) {
	int pos = atoi(stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos")));
	char tmp[8];

	pos += arg->i;
	if(pos < 0)
		pos = 0;
	else if(pos >= selview->nitems)
		pos = selview->nitems - 1;

	snprintf(tmp, sizeof tmp, "%d", pos);
	stfl_set(selview->form, L"pos", stfl_ipool_towc(ipool, tmp));
}

void
viewprev(const Arg *arg) {
	if(!selview->next)
		return;
	selview = selview->next;
}

void
flagas(const Arg *arg) {
}

void
apply(const Arg *arg) {
	if(arg->i) {
		char *opts = "yn";
		if(choose("Apply changes ([y]/n)?", opts) != opts[0])
			return;
	}

	/* XXX ... */

	stfl_setf("statustext", "Changes applied.");
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
setup(void) {
	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, dbhost, dbuser, dbpass, NULL, 0, NULL, 0) == NULL)
		die("Cannot connect to the database.\n");

	ipool = stfl_ipool_create(nl_langinfo(CODESET));
	setmode(NULL);
	stfl_setf("statustext", "Welcome to %s-%s", __FILE__, VERSION);
}

void
cleanup(void) {
	while(views)
		cleanupview(views);
	stfl_reset();
	stfl_ipool_destroy(ipool);
}

void
run(void) {
	Key *k;
	const wchar_t *ev;
	unsigned int i;

	while(running) {
		if(!(ev = stfl_run(selview->form, 0)))
			continue;
		stfl_setf("statustext", "");
		k = NULL;
		for(i = 0; i < LENGTH(keys); ++i)
			if(!((keys[i].mode && strcmp(selview->mode->name, keys[i].mode))
			|| wcscmp(ev, keys[i].modkey)))
				k = &keys[i];
		if(k)
			k->func(&k->arg);
	}
}

int
main(int argc, char **argv) {
	setup();
	run();
	cleanup();
	return 0;
}
