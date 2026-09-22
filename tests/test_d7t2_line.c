#include <assert.h>
#include <stdio.h>
#include <string.h>

int xzs_split_args(char *line, char **argv, int max_argc);

static void
expect(const char *input, int want, const char **words)
{
	char buf[128];
	char *argv[8];
	int argc;
	int i;

	memcpy(buf, input, strlen(input) + 1);
	argc = xzs_split_args(buf, argv, 8);
	assert(argc == want);
	for (i = 0; i < want; i++) {
		assert(strcmp(argv[i], words[i]) == 0);
	}
	assert(argv[want] == 0);
}

int
main(void)
{
	const char *one[] = {"echo", "hello", "xnu"};
	const char *two[] = {"/bin/args", "one", "two", "three"};
	char buf[32];
	char *argv[4];

	expect("", 0, 0);
	expect("   \t  ", 0, 0);
	expect("echo hello xnu", 3, one);
	expect("/bin/args one two three", 4, two);
	memcpy(buf, "a b c d", 8);
	assert(xzs_split_args(buf, argv, 4) == -1);
	printf("d7t2 line tests passed\n");
	return 0;
}
