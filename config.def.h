/* See LICENSE file for copyright and license details. */

#define FLDSEP " | "
#define MAXCOLSZ 19

static const char *dbhost = "";
static const char *dbuser = "";
static const char *dbpass = "";

static void (*welcome)(const Arg *arg) = databases;

static Key keys[] = {
	/* mode          modkey        function        argument */
        { NULL,          L"Q",         quit,           {.i = 1} },
        { NULL,          L"q",         viewprev,       {0} },
        { NULL,          L"k",         itempos,        {.i = -1} },
        { NULL,          L"j",         itempos,        {.i = +1} },
        { NULL,          L"I",         reload,         {0} },
        { NULL,          L"$",         apply,          {.i = 1} },
        { NULL,          L"?",         help,           {0} },
        { "databases",   L"q",         quit,           {.i = 0} },
        { "databases",   L"ENTER",     tables,         {0} },
        { "databases",   L"SPACE",     tables,         {0} },
        { "tables",      L"ENTER",     records,        {.i = 500} },
        { "tables",      L"SPACE",     records,        {.i = 500} },
        { "records",     L"d",         flagas,         {.v = "D"} },
        { "records",     L"t",         flagas,         {.v = "*"} },
};
