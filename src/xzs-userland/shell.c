/* EL0 interactive shell. Commands stay here; the kernel only dispatches syscalls. */

#define SYS_EXIT 1
#define SYS_FORK 2
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_WAIT4 7
#define SYS_CHDIR 12
#define SYS_EXECVE 59
#define SYS_GETDIRENTRIES 196

#define LINE_MAX 128
#define ARG_MAX 8
#define CWD_MAX 96

extern long xzs_svc(long nr, long a, long b, long c, long d);
extern int xzs_split_args(char *line, char **argv, int max_argc);

static int
slen(const char *s)
{
	int n = 0;
	while (s[n] != 0) {
		n++;
	}
	return n;
}

static int
seq(const char *a, const char *b)
{
	while (*a != 0 && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void
wr(const char *s)
{
	(void)xzs_svc(SYS_WRITE, 1, (long)s, slen(s), 0);
}

static void
werr(const char *s)
{
	(void)xzs_svc(SYS_WRITE, 2, (long)s, slen(s), 0);
}

static int
read_line(char *buf, int cap)
{
	int n = 0;

	while (n + 1 < cap) {
		char tmp[64];
		long r = xzs_svc(SYS_READ, 0, (long)tmp, (long)sizeof(tmp), 0);
		long i;

		if (r < 0) {
			buf[0] = 0;
			return -1;
		}
		if (r == 0) {
			break;
		}
		for (i = 0; i < r; i++) {
			char c = tmp[i];
			if (c == '\n' || c == '\r') {
				buf[n] = 0;
				return n;
			}
			if (n + 1 >= cap) {
				buf[0] = 0;
				wr("line too long\n");
				return -2;
			}
			buf[n++] = c;
		}
	}
	buf[n] = 0;
	return n;
}

static void
cmd_help(void)
{
	wr("help echo pwd cd ls cat exit\n");
}

static void
cmd_echo(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (i > 1) {
			wr(" ");
		}
		wr(argv[i]);
	}
	wr("\n");
}

static void
cmd_pwd(const char *cwd)
{
	wr(cwd);
	wr("\n");
}

static int
copy_str(char *dst, int cap, const char *src)
{
	int n = slen(src);
	int i;

	if (n + 1 > cap) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		dst[i] = src[i];
	}
	dst[n] = 0;
	return 0;
}

static void
cmd_cd(int argc, char **argv, char *cwd)
{
	char next[CWD_MAX];
	const char *path;
	long rc;

	if (argc != 2) {
		werr("cd: usage: cd path\n");
		return;
	}
	path = argv[1];
	if (seq(path, ".")) {
		return;
	}
	if (path[0] == '/') {
		if (copy_str(next, CWD_MAX, path) != 0) {
			werr("cd: path too long\n");
			return;
		}
	} else if (seq(path, "..")) {
		int n = slen(cwd);
		int i;
		if (seq(cwd, "/")) {
			return;
		}
		while (n > 1 && cwd[n - 1] != '/') {
			n--;
		}
		if (n > 1) {
			n--;
		}
		for (i = 0; i < n; i++) {
			next[i] = cwd[i];
		}
		next[n] = 0;
		if (next[0] == 0) {
			next[0] = '/';
			next[1] = 0;
		}
		path = next;
	} else {
		int n = slen(cwd);
		int m = slen(path);
		int i;
		int slash = (n > 1);

		if (n + slash + m + 1 > CWD_MAX) {
			werr("cd: path too long\n");
			return;
		}
		for (i = 0; i < n; i++) {
			next[i] = cwd[i];
		}
		if (slash) {
			next[n++] = '/';
		}
		for (i = 0; i < m; i++) {
			next[n++] = path[i];
		}
		next[n] = 0;
		path = next;
	}
	rc = xzs_svc(SYS_CHDIR, (long)path, 0, 0, 0);
	if (rc < 0) {
		werr("cd: failed\n");
		return;
	}
	(void)copy_str(cwd, CWD_MAX, path);
}

static void
cmd_ls(int argc, char **argv, const char *cwd)
{
	const char *path = (argc > 1) ? argv[1] : cwd;
	long fd;
	char buf[256];
	long base = 0;

	fd = xzs_svc(SYS_OPEN, (long)path, 0, 0, 0);
	if (fd < 0) {
		werr("ls: ");
		werr(path);
		werr(": open failed\n");
		return;
	}
	for (;;) {
		long n = xzs_svc(SYS_GETDIRENTRIES, fd, (long)buf, (long)sizeof(buf), (long)&base);
		long off = 0;
		if (n < 0) {
			werr("ls: read failed\n");
			break;
		}
		if (n == 0) {
			break;
		}
		while (off + 21 < n) {
			unsigned short reclen = *(unsigned short *)(buf + off + 16);
			unsigned short namlen = *(unsigned short *)(buf + off + 18);
			if (reclen < 21 || off + reclen > n) {
				break;
			}
			if (namlen > 0 && namlen < 64) {
				char name[64];
				int i;
				for (i = 0; i < namlen; i++) {
					name[i] = buf[off + 21 + i];
				}
				name[namlen] = 0;
				wr(name);
				wr("\n");
			}
			off += reclen;
		}
	}
	(void)xzs_svc(SYS_CLOSE, fd, 0, 0, 0);
}

static void
cmd_cat(int argc, char **argv)
{
	long fd;
	char buf[128];

	if (argc != 2) {
		werr("cat: usage: cat file\n");
		return;
	}
	fd = xzs_svc(SYS_OPEN, (long)argv[1], 0, 0, 0);
	if (fd < 0) {
		werr("cat: ");
		werr(argv[1]);
		werr(": open failed\n");
		return;
	}
	for (;;) {
		long n = xzs_svc(SYS_READ, fd, (long)buf, (long)sizeof(buf), 0);
		if (n < 0) {
			werr("cat: read failed\n");
			break;
		}
		if (n == 0) {
			break;
		}
		(void)xzs_svc(SYS_WRITE, 1, (long)buf, n, 0);
	}
	(void)xzs_svc(SYS_CLOSE, fd, 0, 0, 0);
}

static int
join2(char *dst, int cap, const char *a, const char *b)
{
	int na = slen(a);
	int nb = slen(b);
	int i;
	int slash = (na > 0 && a[na - 1] != '/');

	if (na + slash + nb + 1 > cap) {
		return -1;
	}
	for (i = 0; i < na; i++) {
		dst[i] = a[i];
	}
	if (slash) {
		dst[na++] = '/';
	}
	for (i = 0; i < nb; i++) {
		dst[na++] = b[i];
	}
	dst[na] = 0;
	return 0;
}

static void
run_external(int argc, char **argv)
{
	char path[CWD_MAX];
	const char *file = argv[0];
	long pid;
	int status = 0;
	char *envp[4];

	envp[0] = "PATH=/bin:/sbin";
	envp[1] = "HOME=/";
	envp[2] = "TERM=xzs";
	envp[3] = 0;

	if (file[0] != '/') {
		if (join2(path, CWD_MAX, "/bin", file) != 0) {
			werr(file);
			werr(": command not found\n");
			return;
		}
		file = path;
	}
	pid = xzs_svc(SYS_FORK, 0, 0, 0, 0);
	if (pid < 0) {
		werr("fork: failed\n");
		return;
	}
	if (pid == 0) {
		(void)xzs_svc(SYS_EXECVE, (long)file, (long)argv, (long)envp, 0);
		werr(argv[0]);
		werr(": command not found\n");
		(void)xzs_svc(SYS_EXIT, 127, 0, 0, 0);
		for (;;) {
		}
	}
	(void)xzs_svc(SYS_WAIT4, pid, (long)&status, 0, 0);
}

void
xzs_d7t2_shell(void)
{
	char line[LINE_MAX];
	char *argv[ARG_MAX];
	char cwd[CWD_MAX];

	cwd[0] = '/';
	cwd[1] = 0;
	for (;;) {
		int argc;

		wr("xzs# ");
		if (read_line(line, LINE_MAX) < 0) {
			continue;
		}
		argc = xzs_split_args(line, argv, ARG_MAX);
		if (argc < 0) {
			werr("too many arguments\n");
			continue;
		}
		if (argc == 0) {
			continue;
		}
		if (seq(argv[0], "help")) {
			cmd_help();
		} else if (seq(argv[0], "echo")) {
			cmd_echo(argc, argv);
		} else if (seq(argv[0], "pwd")) {
			cmd_pwd(cwd);
		} else if (seq(argv[0], "cd")) {
			cmd_cd(argc, argv, cwd);
		} else if (seq(argv[0], "ls")) {
			cmd_ls(argc, argv, cwd);
		} else if (seq(argv[0], "cat")) {
			cmd_cat(argc, argv);
		} else if (seq(argv[0], "exit")) {
			(void)xzs_svc(SYS_EXIT, 0, 0, 0, 0);
		} else {
			run_external(argc, argv);
		}
	}
}
