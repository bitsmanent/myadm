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
#include <ctype.h>
#include <mysql.h>
#include <stfl.h>
#include <langinfo.h>
#include <locale.h>
#include <curses.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* STFL fragments (generated via stflfrag) */
#include "fragments.h"

#include "arg.h"
char *argv0;

#define QUOTE(S)		(stfl_ipool_fromwc(ipool, stfl_quote(stfl_ipool_towc(ipool, S))))
#define ISCURVIEW(N)		!(N && selview && strcmp(selview->name, N))
#define LENGTH(X)		(sizeof X / sizeof X[0])

#define MYSQLIDLEN		64
#define MAXQUERYLEN		4096

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	void (*cmd)(void);
} Action;

typedef struct Item Item;
struct Item {
	char **cols;
	int *lens;
	int ncols;
	Item *next;
};

typedef struct Field Field;
struct Field {
	char name[MYSQLIDLEN];
	int len;
	Field *next;
};

typedef struct {
	const char *view;
	const int code;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct View View;
struct View {
	char name[16];
	void (*show)(void);
	Item *items;
	Item *choice;
	Field *fields;
	int cur;
	int nitems;
	int nfields;
	struct stfl_form *form;
	View *next;
};

/* function declarations */
void attach(View *v);
void attachfield(Field *f, Field **ff);
void attachitem(Item *i, Item **ii);
char ui_ask(const char *msg, char *opts);
void cleanup(void);
void cleanupfields(Field **fields);
void cleanupitems(Item **items);
void cleanupview(View *v);
void detach(View *v);
void detachfield(Field *f, Field **ff);
void detachitem(Item *i, Item **ii);
void die(const char *errstr, ...);
void *ecalloc(size_t nmemb, size_t size);
void editfile(char *file);
void editrecord(const Arg *arg);
void edittable(const Arg *arg);
int escape(char *esc, char *s, int sz, char c, char skip);
Item *getitem(int pos);
int *getmaxlengths(Item *items, Field *fields);
void itempos(const Arg *arg);
void mksql_alter_table(char *sql, char *tbl);
void mksql_update(char *sql, Item *item, Field *fields, char *tbl, char *uk);
int mysql_file_exec(char *file);
int mysql_exec(const char *sqlstr, ...);
int mysql_fields(MYSQL_RES *res, Field **fields);
void mysql_fillview(MYSQL_RES *res, int showfds);
int mysql_ukey(char *key, char *tbl, int sz);
int mysql_items(MYSQL_RES *res, Item **items);
void quit(const Arg *arg);
void reload(const Arg *arg);
void run(void);
void setview(const char *name, void (*func)(void));
void setup(void);
void startup(void);
void ui_end(void);
struct stfl_form *ui_getform(wchar_t *code);
void ui_init(void);
void ui_modify(const char *name, const char *mode, const char *fmtstr, ...);
void ui_listview(Item *items, Field *fields);
void ui_putitem(Item *item, int *lens, int id);
void ui_refresh(void);
void ui_set(const char *key, const char *fmtstr, ...);
void ui_showfields(Field *fds, int *lens);
void ui_showitems(Item *items, int *lens);
void ui_sql_edit_exec(char *sql);
void usage(void);
void viewdb(const Arg *arg);
void viewdb_show(void);
void viewdblist(void);
void viewdblist_show(void);
void viewprev(const Arg *arg);
void viewtable(const Arg *arg);
void viewtable_show(void);

#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

#include "config.h"

/* variables */
static int running = 1;
static MYSQL *mysql;
static View *views, *selview = NULL;
static struct stfl_ipool *ipool;
static int fldseplen;

/* function implementations */
void
attach(View *v) {
	v->next = views;
	views = v;
}

void
attachfield(Field *f, Field **ff) {
	Field **l;

	for(l = ff; *l && (*l)->next; l = &(*l)->next);
	if(!*l)
		*l = f;
	else
		(*l)->next = f;
}

void
attachitem(Item *i, Item **ii) {
	Item **l;

	for(l = ii; *l && (*l)->next; l = &(*l)->next);
	if(!*l)
		*l = i;
	else
		(*l)->next = i;
}

char
ui_ask(const char *msg, char *opts) {
	int c;
	char *o = NULL;

	ui_set("status", msg);
	ui_refresh();
	while(!(o && *o) && (c = getch())) {
		if(c == '\n')
			o = &opts[0];
		else
			for(o = opts; *o; ++o)
				if(c == *o)
					break;
	}
	ui_set("status", "");
	return *o;
}

void
cleanup(void) {
	while(views)
		cleanupview(views);
	ui_end();
	mysql_close(mysql);
}

void
cleanupview(View *v) {
	detach(v);
	cleanupitems(&v->items);
	cleanupfields(&v->fields);
	if(v->form)
		stfl_free(v->form);
	free(v);
}

void
cleanupfields(Field **fields) {
	Field *f;

	while(*fields) {
		f = *fields;
		detachfield(f, fields);
		free(f);
	}
}

void
cleanupitems(Item **items) {
	Item *i;

	while(*items) {
		i = *items;
		detachitem(i, items);
		while(--i->ncols >= 0)
			free(i->cols[i->ncols]);
		free(i->cols);
		free(i->lens);
		free(i);
	}
}

void
detach(View *v) {
	View **tv;

	for(tv = &views; *tv && *tv != v; tv = &(*tv)->next);
	*tv = v->next;
}

void
detachfield(Field *f, Field **ff) {
	Field **tf;

	for(tf = &(*ff); *tf && *tf != f; tf = &(*tf)->next);
	*tf = f->next;
}

void
detachitem(Item *i, Item **ii) {
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
		die("Cannot allocate memory.\n");
	return p;
}

void
editfile(char *file) {
        pid_t pid;
	int rc = -1;
	struct sigaction saold[4];

	/* take off ncurses signal handlers */
	struct sigaction sa = {.sa_flags = 0, .sa_handler = SIG_DFL};
	sigaction(SIGINT, &sa, &saold[0]);
	sigaction(SIGTERM, &sa, &saold[1]);
	sigaction(SIGTSTP, &sa, &saold[2]);
	sigaction(SIGWINCH, &sa, &saold[3]);

        if((pid = fork()) == 0) {
                execl("/bin/sh", "sh", "-c", "$EDITOR \"$0\"", file, NULL);
                _exit(127);
        }
	else if(pid == -1)
		return;
	while(!WIFEXITED(rc))
		waitpid(pid, &rc, 0);

	/* restore ncurses signal handlers */
	sigaction(SIGINT, &saold[0], NULL);
	sigaction(SIGTERM, &saold[1], NULL);
	sigaction(SIGTSTP, &saold[2], NULL);
	sigaction(SIGWINCH, &saold[3], NULL);

	/* reinitialize ncurses */
	endwin();
	refresh();
}

void
editrecord(const Arg *arg) {
	Item *item = getitem(0);
	char *tbl = selview->choice->cols[0], uk[MYSQLIDLEN+1], sql[MAXQUERYLEN+1];

	if(!item) {
		ui_set("status", "No item selected.");
		return;
	}
	if(mysql_ukey(uk, tbl, sizeof uk)) {
		ui_set("status", "Cannot edit records in `%s`, no unique key found.", tbl);
		return;
	}
	mksql_update(sql, item, selview->fields, tbl, uk);
	ui_sql_edit_exec(sql);
}

void
edittable(const Arg *arg) {
	Item *item = getitem(0);
	char sql[MAXQUERYLEN+1];

	if(!item->cols[0]) {
		ui_set("status", "No table selected.");
		return;
	}
	/* XXX check alter table permissions */
	mksql_alter_table(sql, item->cols[0]);
	ui_sql_edit_exec(sql);
}

int
escape(char *esc, char *s, int sz, char c, char skip) {
	int i, ei = 0;

	for(i = 0; i < sz; ++i) {
		if(s[i] == c && (!skip || s[i+1] != skip))
			esc[ei++] = '\\';
		esc[ei++] = s[i];
	}
	esc[ei] = '\0';
	return ei - sz;
}

Item *
getitem(int pos) {
	Item *item;
	int i;

	if(!selview)
		return NULL;
	if(!pos)
		pos = selview->cur;
	for(item = selview->items, i = 0; item; item = item->next, ++i)
		if(i == pos)
			break;
	return item;
}

int *
getmaxlengths(Item *items, Field *fields) {
	Item *item;
	Field *fld;
	int i, *lens, ncols;

	if(!(items || fields))
		return NULL;
	if(items)
		ncols = items->ncols;
	else
		for(fld = fields, ncols = 0; fld; fld = fld->next, ++ncols);
	lens = ecalloc(ncols, sizeof(int));
	if(fields)
		for(fld = fields, i = 0; fld; fld = fld->next, ++i)
			lens[i] = (fld->len <= MAXCOLSZ ? fld->len : MAXCOLSZ);
	if(items)
		for(item = items; item; item = item->next)
			for(i = 0; i < item->ncols; ++i)
				if(lens[i] < item->lens[i])
					lens[i] = (item->lens[i] <= MAXCOLSZ ? item->lens[i] : MAXCOLSZ);
	return lens;
}

void
itempos(const Arg *arg) {
	int pos;

	if(!selview || !selview->nitems) {
		ui_set("info", "No items.");
		return;
	}
	pos = selview->cur + arg->i;
	if(pos < 0)
		pos = 0;
	else if(pos >= selview->nitems)
		pos = selview->nitems - 1;
	ui_set("pos", "%d", pos);
	selview->cur = pos;
	ui_set("info", "%d of %d item(s)", selview->cur+1, selview->nitems);
}

void
mksql_alter_table(char *sql, char *tbl) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	char *p;
	int size = MAXQUERYLEN, len = 0, r;

	*sql = '\0';
	r = mysql_exec("show create table `%s`", tbl);
	if(r == -1 || !(res = mysql_store_result(mysql)) || !(row = mysql_fetch_row(res)))
		return;
	mysql_free_result(res);
	len += snprintf(&sql[len], size - len + 1, "ALTER TABLE `%s`", tbl);
	for(r = 0, p = &row[1][0]; row[1][r]; ++r) {
		if(row[1][r] != '\n')
			continue;
		while(*p == ' ')
			++p;
		if(*p == '`') {
			row[1][r] = '\0';
			len += snprintf(&sql[len], size - len + 1, "\nMODIFY %s", p);
		}
		p = &row[1][r + 1];
	}
	if(sql[len - 1] == ',')
		sql[len - 1] = '\0';
}

void
mksql_update(char *sql, Item *item, Field *fields, char *tbl, char *uk) {
	Field *fld;
	char *ukv = NULL, sqlfds[MAXQUERYLEN+1], col[MAXQUERYLEN];
	int size = MAXQUERYLEN, len = 0, i;

	for(i = 0, fld = fields; fld; fld = fld->next, ++i) {
		if(!ukv && !strncmp(uk, fld->name, fld->len))
			ukv = item->cols[i];
		escape(col, item->cols[i], item->lens[i], '\'', 0);
		len += snprintf(&sqlfds[len], size - len + 1, "\n%c`%s` = '%s'",
			len ? ',' : ' ', fld->name, col);
	}
	snprintf(sql, MAXQUERYLEN+1, "UPDATE `%s` SET%s\nWHERE `%s` = '%s'",
		tbl, sqlfds, uk, ukv);
}

int
mysql_exec(const char *sqlstr, ...) {
	va_list ap;
	char sql[MAXQUERYLEN+1];
	int r, sqlen;

	va_start(ap, sqlstr);
	sqlen = vsnprintf(sql, sizeof sql, sqlstr, ap);
	va_end(ap);
	r = mysql_real_query(mysql, sql, sqlen);
	return (r ? -1 : mysql_field_count(mysql));
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
		attachfield(field, fields);
	}
	return nfds;
}

int
mysql_file_exec(char *file) {
	char buf[MAXQUERYLEN+1], esc[MAXQUERYLEN*2+1];
	int fd, size, r;

	fd = open(file, O_RDONLY);
	if(fd == -1)
		return -1;
	size = read(fd, buf, MAXQUERYLEN);
	if(size == -1)
		return -2;
	if(!size)
		return 0;
	buf[size] = '\0';
	/* We do not want flow control chars to be interpreted. */
	size += escape(esc, buf, size, '\\', '\'');
	r = mysql_exec(esc);
	if(r == -1)
		return -3;
	return size;
}

void
mysql_fillview(MYSQL_RES *res, int showfds) {
	cleanupitems(&selview->items);
	selview->nitems = mysql_items(res, &selview->items);
	if(showfds) {
		cleanupfields(&selview->fields);
		selview->nfields = mysql_fields(res, &selview->fields);
	}
}

int
mysql_ukey(char *key, char *tbl, int sz) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	int r;

	r = mysql_exec("show keys from `%s` where Non_unique = 0", tbl);
	if(r == -1 || !(res = mysql_store_result(mysql)))
		return 1;
	if(!(row = mysql_fetch_row(res))) {
		mysql_free_result(res);
		return 2;
	}
	snprintf(key, sz, "%s", row[4]);
	mysql_free_result(res);
	return 0;
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
			item->cols[i] = ecalloc(1, lens[i]+1);
			memcpy(item->cols[i], row[i], lens[i]);
			item->lens[i] = lens[i];
		}
		attachitem(item, items);
	}
	return nrows;
}

void
ui_listview(Item *items, Field *fields) {
	int *lens;

	if(!selview->form)
		selview->form = ui_getform(FRAG_ITEMS);
	lens = getmaxlengths(items, fields);
	if(fields)
		ui_showfields(fields, lens);
	if(items)
		ui_showitems(items, lens);
	free(lens);
}

void
ui_showfields(Field *fds, int *lens) {
	Field *fld;
	char line[COLS + 1];
	int li = 0, i, j;

	if(!(fds && lens))
		return;
	line[0] = '\0';
	for(fld = fds, i = 0; fld && li < COLS; fld = fld->next, ++i) {
		if(i)
			for(j = 0; j < fldseplen && li < COLS; ++j)
				line[li++] = FLDSEP[j];
		for(j = 0; li < COLS && j < fld->len && j < lens[i]; ++j)
			line[li++] = fld->name[j];
		while(li < COLS && j++ < lens[i])
			line[li++] = ' ';
	}
	line[li] = '\0';
	ui_set("subtle", "%s", line);
	ui_set("showsubtle", "%d", (line[0] ? 1 : 0));
}

void
ui_showitems(Item *items, int *lens) {
	Item *item;
	int id = 0;

	ui_modify("items", "replace_inner", "vbox"); /* empty items */
	for(item = selview->items; item; item = item->next)
		ui_putitem(item, lens, ++id);
	ui_set("pos", "0");
}

void
ui_sql_edit_exec(char *sql) {
	struct stat sb, sa;
	int fd;
	char tmpf[] = "/tmp/myadm.XXXXXX";

        fd = mkstemp(tmpf);
	if(fd == -1) {
		ui_set("status", "Cannot make a temporary file.");
		return;
	}
        if(write(fd, sql, strlen(sql)) == -1) {
		close(fd);
		unlink(tmpf);
		ui_set("status", "Cannot write into the temporary file.");
		return;
	}
	close(fd);
	stat(tmpf, &sb);
	while(1) {
		editfile(tmpf);
		stat(tmpf, &sa);
		if(!sa.st_size || sb.st_mtime == sa.st_mtime) {
			ui_set("status", "No changes.");
			break;
		}
		if(mysql_file_exec(tmpf) < 0) {
			if(*mysql_error(mysql)) {
				if(ui_ask("An error occurred. Continue editing ([y]/n)?", "yn") == 'y')
					continue;
			}
			else
				ui_set("status", "Something went wrong.");
			break;
		}
		reload(NULL);
		ui_set("status", "Updated.");
		break;
	}
	unlink(tmpf);
}

void
quit(const Arg *arg) {
	if(arg->i && ui_ask("Do you want to quit ([y]/n)?", "yn") != 'y')
		return;
	running = 0;
}

void
reload(const Arg *arg) {
	if(!selview->show)
		return;
	selview->show();
	if(selview->cur)
		ui_set("pos", "%d", selview->cur);
}

void
run(void) {
	int code, i;

	while(running) {
		ui_refresh();
		code = getch();
		if(code < 0)
			continue;
		for(i = 0; i < LENGTH(keys); ++i) {
			if(ISCURVIEW(keys[i].view) && keys[i].code == code) {
				ui_set("status", "");
				keys[i].func(&keys[i].arg);
				break;
			}
		}
	}
}

void
setview(const char *name, void (*show)(void)) {
	View *v;

	v = ecalloc(1, sizeof(View));
	v->choice = getitem(0);
	strncpy(v->name, name, sizeof v->name);
	v->show = show;
	attach(v);
	selview = v;
	show();
}

void
setup(void) {
	setlocale(LC_CTYPE, "");
	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, dbhost, dbuser, dbpass, NULL, 0, NULL, 0) == NULL)
		die("Cannot connect to the database.\n");
	fldseplen = strlen(FLDSEP);
	ui_init();
}

void
startup(void) {
	for(unsigned int i = 0; i < LENGTH(actions); i++)
		actions[i].cmd();
}

void
ui_end(void) {
	stfl_reset();
	stfl_ipool_destroy(ipool);
}

struct stfl_form *
ui_getform(wchar_t *code) {
	return stfl_create(code);
}

void
ui_init(void) {
	struct stfl_form *f = ui_getform(L"label");

	stfl_run(f, -3); /* init ncurses */
	stfl_free(f);
	nocbreak();
	raw();
	curs_set(0);
	ipool = stfl_ipool_create(nl_langinfo(CODESET));
}

void
ui_modify(const char *name, const char *mode, const char *fmtstr, ...) {
	va_list ap;
	char txt[1024];

	if(!selview->form)
		return;
	va_start(ap, fmtstr);
	vsnprintf(txt, sizeof txt, fmtstr, ap);
	va_end(ap);
	stfl_modify(selview->form,
		stfl_ipool_towc(ipool, name),
		stfl_ipool_towc(ipool, mode),
		stfl_ipool_towc(ipool, txt));
}

void
ui_putitem(Item *item, int *lens, int id) {
	char line[COLS + 1];
	int pad, li = 0, i, j;

	if(!(item && lens))
		return;
	line[0] = '\0';
	for(i = 0; i < item->ncols && li < COLS; ++i) {
		if(i)
			for(j = 0; j < fldseplen && li < COLS; ++j)
				line[li++] = FLDSEP[j];
		pad = li;
		for(j = 0; j < item->lens[i] && j < lens[i] && li < COLS; ++j)
			line[li++] = (isprint(item->cols[i][j])
					? item->cols[i][j]
					: ' ');
		pad = li - pad;
		while(pad++ < lens[i] && li < COLS)
			line[li++] = ' ';
	}
	line[li] = '\0';
	ui_modify("items", "append", "listitem[%d] text:%s", id, QUOTE(line));
}

void
ui_refresh(void) {
	if(selview && selview->form)
		stfl_run(selview->form, -1);
}

void
ui_set(const char *key, const char *fmtstr, ...) {
	va_list ap;
	char val[256];

	if(!selview)
		return;

	va_start(ap, fmtstr);
	vsnprintf(val, sizeof val, fmtstr, ap);
	va_end(ap);
	stfl_set(selview->form, stfl_ipool_towc(ipool, key), stfl_ipool_towc(ipool, val));
}

void
usage(void) {
	die("Usage: %s [-vhup <arg>]\n", argv0);
}

void
viewdb(const Arg *arg) {
	Arg a = {.i = 0};
	Item *choice = getitem(0);

	if(!choice) {
		ui_set("status", "No database selected.");
		return;
	}
	mysql_select_db(mysql, choice->cols[0]);
	setview("tables", viewdb_show);
	itempos(&a);
}

void
viewdb_show(void) {
	MYSQL_RES *res;

	if(mysql_exec("show tables") == -1 || !(res = mysql_store_result(mysql)))
		die("show tables");
	mysql_fillview(res, 0);
	mysql_free_result(res);
	ui_listview(selview->items, NULL);
	ui_set("title", "Tables in `%s`@%s", selview->choice->cols[0], dbhost);
}

void
viewdblist(void) {
	Arg a = {.i = 0};

	setview("databases", viewdblist_show);
	itempos(&a);
}

void
viewdblist_show(void) {
	MYSQL_RES *res;

	if(mysql_exec("show databases") == -1 || !(res = mysql_store_result(mysql)))
		die("show databases");
	mysql_fillview(res, 0);
	mysql_free_result(res);
	ui_listview(selview->items, NULL);
	ui_set("title", "Databases in `%s`", dbhost);
}

void
viewprev(const Arg *arg) {
	View *v;

	if(!(selview && selview->next))
		return;
	v = selview->next;
	cleanupview(selview);
	selview = v;
}

void
viewtable(const Arg *arg) {
	Arg a = {.i = 0};

	if(!getitem(0)) {
		ui_set("status", "No table selected.");
		return;
	}
	setview("records", viewtable_show);
	itempos(&a);
}

void
viewtable_show(void) {
	MYSQL_RES *res;
	int r;

	r = mysql_exec("select * from `%s`", selview->choice->cols[0]);
	if(r == -1 || !(res = mysql_store_result(mysql)))
		die("select from `%s`", selview->choice->cols[0]);
	mysql_fillview(res, 1);
	mysql_free_result(res);
	ui_listview(selview->items, selview->fields);
	ui_set("title", "Records in `%s`.`%s`@%s",
		selview->next->choice->cols[0], selview->choice->cols[0], dbhost);
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
		die("%s-"VERSION, argv0);
	default:
		usage();
	} ARGEND;
	setup();
	startup();
	run();
	cleanup();
	return 0;
}
