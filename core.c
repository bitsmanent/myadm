/* cc -D_BSD_SOURCE -std=c99 -O0 -Wall -pedantic -o core core.c $(mysql_config --cflags) -lmysqlclient -lstfl -lncursesw */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <mysql.h>
#include <stfl.h>

#include <langinfo.h>
#include <locale.h>

#define LENGTH(X)               (sizeof X / sizeof X[0])

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Item Item;
struct Item {
	char *name;
	int len;
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
	const char *mode;
	const wchar_t *modkey;
	const wchar_t *var;
} Stflkey;

typedef struct {
	const char *name;
	void (*func)(void);
} Mode;

typedef struct View View;
struct View {
	Mode *mode;
	Item *items;
	struct stfl_form *form;
	View *next;
};

/* function declarations */
void die(const char *errstr, ...);
void databases(void);
void tables(void);
void records(void);
void text(void);
MYSQL_RES *mysql_exec(char *sql);
Item *mysql_items(MYSQL_RES *res);
void attach(View *v);
void detach(View *v);
void *ecalloc(size_t nmemb, size_t size);
void cleanupview(View *v);
void flagas(const Arg *arg);
void apply(const Arg *arg);
void quit(const Arg *arg);
void setmode(const Arg *arg);
void viewprev(const Arg *arg);
void usedb(const Arg *arg);
void usetable(const Arg *arg);
void userecord(const Arg *arg);

/* config.h > */

/* XXX command line arguments */
#define DBHOST "localhost"
#define DBUSER "root"
#define DBPASS "m0r3s3cur3"

static Mode modes[] = {
	/* name         show function */
	{ "databases",  databases }, /* first entry is default */
	{ "tables",     tables },
	{ "records",    records },
	{ "text",       text },
};

static Key keys[] = {
	/* mode          modkey        function        argument */
        { NULL,          L"Q",         quit,           {.i = 0} },
        { NULL,          L"q",         viewprev,       {0} },
        { NULL,          L"D",         setmode,        {.v = &modes[0]} },
        { NULL,          L"T",         setmode,        {.v = &modes[1]} },
        { NULL,          L"R",         setmode,        {.v = &modes[2]} },
        { NULL,          L"E",         setmode,        {.v = &modes[3]} },
        { "databases",   L"q",         quit,           {.i = 1} },
        { "databases",   L"ENTER",     usedb,          {.v = &modes[1]} },
        { "tables",      L"ENTER",     usetable,       {0} },
        { "records",     L"ENTER",     userecord,      {0} },
        { "records",     L"d",         flagas,         {.v = "D"} },
        { "records",     L"t",         flagas,         {.v = "*"} },
        { "records",     L"$",         apply,          {0} },
};

static Stflkey stflkeys[] = {
	/* mode          modkey        stfl variable */
	{ NULL,          L"k UP",      L"bind_up" },
	{ NULL,          L"j DOWN",    L"bind_down" },
};

/* < config.h */

/* variables */
static int running = 1;
static MYSQL *mysql = NULL;
static View *views, *selview;
static struct stfl_ipool *ipool;

/* function implementations */
void
quit(const Arg *arg) {
	if(arg->i) {
		/* XXX ask for confirmation */
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

	for (tv = &views; *tv && *tv != v; tv = &(*tv)->next);
	*tv = v->next;
}

void
setmode(const Arg *arg) {
	const Mode *m = arg->v;
	View *v;
	unsigned int i;

	if(selview && !strcmp(arg->v, m->name))
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

		selview = v;
		selview->mode->func();

		/* bind stfl keys */
		for(i = 0; i < LENGTH(stflkeys); ++i)
			if(!(stflkeys[i].mode && strcmp(stflkeys[i].mode, selview->mode->name)))
				stfl_set(selview->form, stflkeys[i].var, stflkeys[i].modkey);
	}
	else {
		selview = v;
		selview->mode->func();
	}

	/* XXX Throwing a visual error instead of die... */
	if(!selview->form)
		die("%s: mode is broken.\n", m->name);
}

void
cleanupview(View *v) {
	detach(v);

	/* XXX
	while(v->items)
		cleanupitem(v->items);
	*/

	/* XXX cleanup items */
	if(v->form)
		stfl_free(v->form);
	free(v);
}

MYSQL_RES *
mysql_exec(char *sql) {
	MYSQL_RES *res;

	if(mysql_real_query(mysql, sql, strlen(sql)))
		return NULL;
	res = mysql_store_result(mysql);
	if(!res)
		return NULL; /* XXX if(mysql_field_count(mysql)) error; */
	return res;
}

Item *
mysql_items(MYSQL_RES *res) {
	MYSQL_ROW row;
	Item *items, *item;
	int i, nfds;

	nfds = mysql_num_fields(res);

	items = NULL;
	while((row = mysql_fetch_row(res))) {
		item = ecalloc(1, sizeof(Item));
		item->name = ecalloc(256, sizeof(char));

		for(i = 0; i < nfds; ++i)
			snprintf(item->name, 22, "%s", row[i]);

		item->next = items;
		items = item;
	}

	return items;
}

void
databases(void) {
	MYSQL_RES *res;
	Item *item;
	char txt[256];
	int i;

	if(selview->form)
		return;

	if(!(res = mysql_exec("show databases")))
		die("databases");

	/* XXX Previously allocated items are never freed, only the LAST
	 * allocation gets freed by cleanupview(). */
	selview->items = mysql_items(res);
	mysql_free_result(res);

	selview->form = stfl_create(L"<databases.stfl>");
	i = 0;
	for(item = selview->items; item; item = item->next) {
		snprintf(txt, sizeof txt, "listitem[%d] text:\"%s\"", i++, item->name);
		stfl_modify(selview->form, L"databases", L"append", stfl_ipool_towc(ipool, txt));
	}
}

void
tables(void) {
	MYSQL_RES *res;
	Item *item;
	char txt[256];
	int i;

	if(!(res = mysql_exec("show tables")))
		die("tables\n");

	/*
	XXX
	if(selview->items)
		free items
	*/

	selview->items = mysql_items(res);
	mysql_free_result(res);

	if(!selview->form)
		selview->form = stfl_create(L"<tables.stfl>");
	i = 0;
	stfl_modify(selview->form, L"tables", L"replace_inner", L"vbox"); /* clear */
	for(item = selview->items; item; item = item->next) {
		snprintf(txt, sizeof txt, "listitem[%d] text:\"%s\"", i++, item->name);
		stfl_modify(selview->form, L"tables", L"append", stfl_ipool_towc(ipool, txt));
	}
}

void
records(void) {
}

void
text(void) {
}

void
usedb(const Arg *arg) {
	Item *item;
	int i = 0;

	int pos = atoi(stfl_ipool_fromwc(ipool, stfl_get(selview->form, L"pos")));

	for(item = selview->items; item; item = item->next)
		if(i++ == pos)
			break;

	mysql_select_db(mysql, item->name);
	setmode(arg);
}

void
usetable(const Arg *arg) {
}

void
userecord(const Arg *arg) {
}

void
viewprev(const Arg *arg) {
	if(!selview->next)
		return;
	selview = selview->next;
	selview->mode->func();
}

void
flagas(const Arg *arg) {
}

void
apply(const Arg *arg) {
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

int
main(int argc, char **argv) {
	Arg a = {.v = &modes[0]};
	Key *k;
	const wchar_t *ev;
	unsigned int i;

	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, DBHOST, DBUSER, DBPASS, NULL, 0, NULL, 0) == NULL)
		die("Cannot connect to the database.\n");
	
	ipool = stfl_ipool_create(nl_langinfo(CODESET));
	setmode(&a);

	while(running) {
		if(!(ev = stfl_run(selview->form, 0)))
			continue;

		k = NULL;
		for(i = 0; i < LENGTH(keys); ++i)
			if(!((keys[i].mode && strcmp(selview->mode->name, keys[i].mode))
			|| wcscmp(ev, keys[i].modkey)))
				k = &keys[i];
		if(k)
			k->func(&k->arg);
	}

	while(views)
		cleanupview(views);

	stfl_reset();
	stfl_ipool_destroy(ipool);
	return 0;
}
