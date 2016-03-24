/* See LICENSE file for copyright and license details. */

#define FLDSEP " | "
#define MAXCOLSZ 19

static const char *dbhost = "";
static const char *dbuser = "";
static const char *dbpass = "";

#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

static Key keys[] = {
	/* mode          key           function        argument */
        { NULL,          'Q',          quit,           {.i = 1} },
        { NULL,          'q',          viewprev,       {0} },
        { NULL,          'k',          itemsel,        {.i = -1} },
        { NULL,          KEY_UP,       itemsel,        {.i = -1} },
        { NULL,          'j',          itemsel,        {.i = +1} },
        { NULL,          KEY_DOWN,     itemsel,        {.i = +1} },
        { NULL,          'I',          reload,         {0} },
        { "databases",   'q',          quit,           {.i = 0} },
        { "databases",   '\n',         viewdb,         {0} },
        { "databases",   ' ',          viewdb,         {0} },
        { "tables",      '\n',         viewtable,      {0} },
        { "tables",      ' ',          viewtable,      {0} },
};
