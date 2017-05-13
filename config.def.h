/* See LICENSE file for copyright and license details. */

#define FLDSEP " | "
#define MAXCOLSZ 19

static const char *dbhost = "";
static const char *dbuser = "";
static const char *dbpass = "";

/* gets executed when myadm is started */
static Action actions[] = {
	{ viewdblist },
};

static Key keys[] = {
	/* view          key           function        argument */
        { "databases",   'q',          quit,           {.i = 0} },
        { "databases",   '\n',         viewdb,         {0} },
        { "databases",   ' ',          viewdb,         {0} },
        { "tables",      '\n',         viewtable,      {0} },
        { "tables",      ' ',          viewtable,      {0} },
        { "tables",      'e',          edittable,      {0} },
        { "records",     'e',          editrecord,     {0} },
        { "records",     ' ',          editrecord,     {0} },
        { NULL,          CTRL('c'),    quit,           {.i = 1} },
        { NULL,          'Q',          quit,           {.i = 1} },
        { NULL,          'q',          viewprev,       {0} },
        { NULL,          'k',          itempos,        {.i = -1} },
        { NULL,          KEY_UP,       itempos,        {.i = -1} },
        { NULL,          'j',          itempos,        {.i = +1} },
        { NULL,          KEY_DOWN,     itempos,        {.i = +1} },
        { NULL,          CTRL('d'),    itempos,        {.i = +20} },
        { NULL,          KEY_NPAGE,    itempos,        {.i = +20} },
        { NULL,          CTRL('u'),    itempos,        {.i = -20} },
        { NULL,          KEY_PPAGE,    itempos,        {.i = -20} },
        { NULL,          'I',          reload,         {0} },
};
