/* Bounded line split for the EL0 shell. No allocation. */

int
xzs_split_args(char *line, char **argv, int max_argc)
{
	int argc = 0;
	char *p = line;

	if (line == 0 || argv == 0 || max_argc < 1) {
		return -1;
	}
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p == 0) {
		argv[0] = 0;
		return 0;
	}
	while (*p != 0) {
		if (argc + 1 >= max_argc) {
			return -1;
		}
		argv[argc++] = p;
		while (*p != 0 && *p != ' ' && *p != '\t') {
			p++;
		}
		if (*p == 0) {
			break;
		}
		*p++ = 0;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
	}
	argv[argc] = 0;
	return argc;
}
