/* See LICENSE file for copyright and license details. */

#define FLDSEP " | "
#define MAXCOLSZ 19

static const char *dbhost = "";
static const char *dbuser = "";
static const char *dbpass = "";

static void (*welcome)(const Arg *arg) = databases;

static Key keys[] = {
	/* mode          modkey        function        argument */
        { NULL,          "Q",          quit,           {.i = 0} },
        { NULL,          "q",          viewprev,       {0} },
        { NULL,          "k",          itempos,        {.i = -1} },
        { NULL,          "j",          itempos,        {.i = +1} },
        { NULL,          "I",          reload,         {0} },
        { NULL,          "$",          apply,          {.i = 1} },
        { NULL,          "?",          help,           {0} },
        { "databases",   "q",          quit,           {.i = 0} },
        { "databases",   "ENTER",      tables,         {0} },
        { "databases",   "SPACE",      tables,         {0} },
        { "tables",      "ENTER",      records,        {.i = 500} },
        { "tables",      "SPACE",      records,        {.i = 500} },
        { "records",     "d",          flagas,         {.v = "D"} },
        { "records",     "t",          flagas,         {.v = "*"} },
};
