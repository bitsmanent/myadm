/* cc -D_BSD_SOURCE -std=c99 -O0 -Wall -pedantic -o test test.c $(mysql_config --cflags) -lmysqlclient */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <mysql.h>

typedef struct {
	char *name;
	int len;
	unsigned int flags;
} Item;

typedef struct {
	char **fields;
	Item **items;
	int nfields;
	int nvalues;
} List;

typedef struct {
	MYSQL_FIELD *fields;
	MYSQL_RES *res;
	int nfields;
	int nrows;
} Query;


/* globals */
static MYSQL *mysql = NULL;

/* function declarations */
/* ... */

/* function implementations */
Item *
newitem(char *name, int len) {
	Item *item;

	item = malloc(sizeof(Item));
	item->name = malloc(sizeof(char) * len + 1);
	snprintf(item->name, len + 1, "%s", name);
	item->len = len;
	item->flags = 0;
	return item;
}

List *
newlist(int nfields, int nvalues, char **fields, Item **items) {
	List *list;

	list = malloc(sizeof(List));
	if(!list)
		return NULL;
	list->fields = fields;
	list->items = items;
	list->nfields = nfields;
	list->nvalues = nvalues;
	return list;
}

void
free_list(List *l) {
	int i;

	for(i = 0; i < l->nvalues; ++i)
		free(l->items[i]);
	free(l);
}

void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

/* XXX to be implemented */
Query *
mysql_vexec(char *sqlstr, ...) {
	return NULL;
}

Query *
mysql_exec(char *sql, Query *qry) {
	MYSQL_RES *res;
	int nfds;

	if(mysql_real_query(mysql, sql, strlen(sql)))
		return NULL;
	res = mysql_store_result(mysql);
	if(!res)
		return NULL; /* XXX if(mysql_field_count(mysql)) error; */
	nfds = mysql_num_fields(res);
	if(!qry)
		qry = malloc(sizeof(Query));
	qry->res = res;
	qry->nfields = nfds;
	qry->nrows = mysql_num_rows(res);
	qry->fields = mysql_fetch_fields(res);
	return qry;
}

void
mysql_free(Query *q) {
	mysql_free_result(q->res);
	free(q);
}

int
mysql_main() {
	mysql = mysql_init(NULL);
	if(mysql_real_connect(mysql, "localhost", "root", "m0r3s3cur3", NULL, 0, NULL, 0) == NULL)
		return 1;
	return 0;
}

void
mysql_end() {
	mysql_close(mysql);
}

const char *
mysql_err() {
	return mysql_error(mysql);
}

List *
mysql_query_to_list(Query *q) {
	MYSQL_ROW row;
	unsigned long *lens;
	Item **items;
	char **fields;
	int i, j, len;

	/* fields */
	fields = malloc(sizeof(char *) * q->nfields);
	for(i = 0; i < q->nfields; ++i) {
		len = strlen(q->fields[i].name) + 1;
		fields[i] = malloc(sizeof(char) * len);
		snprintf(fields[i], len, "%s", q->fields[i].name);
	}

	/* items */
	items = malloc(sizeof(Item *) * q->nrows * q->nfields);
	for(i = 0; i < q->nrows; ++i) {
		row = mysql_fetch_row(q->res);
		lens = mysql_fetch_lengths(q->res);

		for(j = 0; j < q->nfields; ++j)
			items[i * q->nfields + j] = newitem(row[j], lens[j]);
	}

	return newlist(q->nfields, q->nrows, fields, items);
}

void
d_query(Query *q) {
	MYSQL_ROW row;
	int i, j, nfds;

	printf("%d field(s), %d row(s) queried.\n", q->nfields, q->nrows);
	nfds = (q->nfields > 5 ? 5 : q->nfields); /* show max 5 cols */

	for(i = 0; i < nfds; ++i)
		printf("%18.16s", q->fields[i].name);
	printf("\n");

	mysql_data_seek(q->res, 0);
	for(i = 0; i < q->nrows; ++i) {
		row = mysql_fetch_row(q->res);
		for(j = 0; j < nfds; ++j)
			printf("%18.16s", row[j]);
		printf("\n");
	}
}

void
d_list(List *l) {
	int i, j, nfds;

	printf("%d field(s), %d row(s) queried.\n", l->nfields, l->nvalues);
	nfds = (l->nfields > 5 ? 5 : l->nfields); /* show max 5 cols */

	for(i = 0; i < nfds; ++i)
		printf("%18.16s", l->fields[i]);
	printf("\n");

	for(i = 0; i < l->nvalues; ++i) {
		for(j = 0; j < nfds; ++j)
			printf("%18.16s", l->items[i * l->nfields + j]->name);
		printf("\n");
	}
}

int
main(int argc, char *argv[]) {
	List *l;
	Query *q;
	int i;

	if(mysql_main())
		die("mysql_main()");
	q = NULL;
	for(i = 1; i < argc; ++i) {
		if(!(q = mysql_exec(argv[i], q))) {
			printf("%s\n", mysql_err());
			continue;
		}
		l = mysql_query_to_list(q);
		//d_query(q);
		if(!l) {
			printf("Cannot convert to list.\n");
			continue;
		}
		d_list(l);
		free_list(l);
	}
	if(q)
		mysql_free(q);
	mysql_end();
	return 0;
}
