#include <langinfo.h>
#include <stdio.h>
#include <stfl.h>
#include <string.h>

int
main(int argc, char **argv) {
	struct stfl_form *form;
	struct stfl_ipool *ipool;
	const wchar_t *frag;
	wchar_t file[256];

	if(argc != 2)
		return 1;
	ipool = stfl_ipool_create(nl_langinfo(CODESET));
	swprintf(file, sizeof file - 1, L"<%ls>", stfl_ipool_towc(ipool, argv[1]));
	if(!(form = stfl_create(file)))
		return 1;
	frag = stfl_dump(form, NULL, NULL, 0);
	wprintf(L"%ls", frag);
	stfl_ipool_destroy(ipool);
	return 0;
}
