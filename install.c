/*
 * This program is based on Rich Felker's comments in commit e678fc6f ("replace
 * system's install command with a shell script", 2013-08-17) of musl.git.
 *
 * Really, you should use the shell script instead of this.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char install_help[] =
"usage: %s [-Dghlmor] <src> <dst>\n"
"\n"
"Options:\n"
" -D           Create parent directories of <dst>. Directories are created\n"
"              with the default umask(1).\n"
" -g <gid>     Set the group of the installed file to <gid>. This may be\n"
"              either a numeric group ID or a group name.\n"
" -h           Display this help text.\n"
" -l           Install as a symbolic link.\n"
" -m <mode>    Set the permissions of the installed file to <mode>. This\n"
"              must be a valid octal mode.\n"
" -o <uid>     Set the owner of the installed file to <uid>. This may be\n"
"              either a numeric user ID or a user name.\n";

static void fatal(const char *fmt, ...)
{
	char buf[4096];
	snprintf(buf, 4096, "fatal: %s\n", fmt);

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, buf, args);
	va_end(args);

	exit(1);
}

static int mkparents(const char *path)
{
	assert(path);

	size_t l = strlen(path);
	const char *p = path + l;

	while (l > 0 && *p != '/') {
		l--;
		p--;
	}

	if (l == 0)
		return 0;

	char *buf = malloc(l + 1);
	if (!buf)
		goto failure;

	memset(buf, '\0', l + 1);
	memcpy(buf, path, l);

	if (mkparents(buf) == -1)
		goto failure;

	mkdir(buf, 0777);
	free(buf);

	return 0;

failure:
	free(buf);
	return -1;
}

int main(int argc, char **argv)
{
	int mkdirp = 0;
	int symbolic = 0;

	uid_t owner = getuid();
	gid_t group = getgid();
	mode_t mode = 0755;

	struct passwd *p = NULL;
	struct group *g = NULL;
	char *err = NULL;

	char *src_path = NULL;
	char *dst_path = NULL;
	char *tmp_path = NULL;

	int opt = -1;
	while ((opt = getopt(argc, argv, ":Dg:hlm:o:r")) != -1) {
		switch(opt) {
		case 'D':
			mkdirp = 1;
			break;
		case 'g':
			errno = 0;
			group = strtoul(optarg, &err, 0);

			if (errno != 0 || (err && *err != '\0')) {
				g = getgrnam(optarg);
				if (!g)
					fatal("invalid group: '%s'", optarg);

				group = g->gr_gid;
			} else if (errno != 0) {
				fatal("invalid group: '%s'", optarg);
			}

			err = NULL;
			break;
		case 'h':
			printf(install_help, argv[0]);
			exit(0);
			break;
		case 'l':
			symbolic = 1;
			break;
		case 'm':
			errno = 0;
			mode = strtoul(optarg, &err, 8);

			if (errno != 0 || (err && *err != '\0'))
				fatal("invalid mode: %s", optarg);
			break;
		case 'o':
			errno = 0;
			owner = strtoul(optarg, &err, 0);

			if (errno != 0 || (err && *err != '\0')) {
				p = getpwnam(optarg);
				if (!p)
					fatal("invalid user: '%s'", optarg);

				owner = p->pw_uid;
			} else if (errno != 0) {
				fatal("invalid user: '%s'", optarg);
			}

			err = NULL;
			break;
		case ':':
			fatal("option requires argument: -%c", optopt);
			break;
		default:
			fatal("invalid option: -%c", optopt);
			break;
		}
	}

	if (argc - optind != 2)
		fatal("expected arguments: <src> <dst>");

	if (mode > 07777)
		fatal("invalid mode: %u", mode);

	src_path = argv[optind];
	dst_path = argv[optind + 1];

	if (!(tmp_path = malloc(strlen(dst_path) + 5)))
		fatal("malloc(3) failure");

	errno = 0;
	if (snprintf(tmp_path, strlen(dst_path) + 5, "%s.tmp", dst_path) < 0)
		fatal("snprintf(3) failure: %s", strerror(errno));

	errno = 0;
	if (mkdirp && mkparents(dst_path) == -1) {
		fatal("failed to create directories: %s (%s)",
		      dst_path, strerror(errno));
	}

	if (symbolic) {
		errno = 0;
		if (symlink(src_path, tmp_path) == -1)
			fatal("symlink(3) failure: %s", strerror(errno));
	} else {
		int src_fd = -1;
		int tmp_fd = -1;
		struct stat src_stat;

		errno = 0;
		if (lstat(src_path, &src_stat) == -1) {
			if (errno == ENOENT)
				fatal("source does not exist: %s", src_path);
			else
				fatal("lstat(3) failure: %s", strerror(errno));
		}

		if (!S_ISLNK(src_stat.st_mode) && !S_ISREG(src_stat.st_mode)) {
			if (S_ISDIR(src_stat.st_mode))
				fatal("source is a directory: %s", src_path);
			else
				fatal("source is not a regular file: %s", src_path);
		}

		errno = 0;
		if ((src_fd = open(src_path, O_RDONLY)) == -1) {
			fatal("failed to open file: %s (%s)",
			      src_path, strerror(errno));
		}

		errno = 0;
		if ((tmp_fd = open(tmp_path, O_RDWR | O_CREAT, 0600)) == -1) {
			fatal("failed to create temporary file: %s (%s)",
			      tmp_path, strerror(errno));
		}

		char *buf = malloc(src_stat.st_blksize);
		if (!buf)
			fatal("malloc(3) failure");

		ssize_t r = 0;
		while ((r = read(src_fd, buf, src_stat.st_blksize)) > 0) {
			errno = 0;
			if (write(tmp_fd, buf, r) == -1)
				fatal("write(3) failure: %s", strerror(errno));
		}
	}

	errno = 0;
	if (chmod(tmp_path, mode) == -1)
		fatal("chmod(3) failure: %s", strerror(errno));

	if (lchown(tmp_path, owner, group) == -1)
		fatal("lchown(3) failure: %s", strerror(errno));

	if (rename(tmp_path, dst_path) == -1)
		fatal("rename(3) failure: %s", strerror(errno));

	return 0;
}
