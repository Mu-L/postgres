/*
 *	option.c
 *
 *	options functions
 *
 *	Copyright (c) 2010-2025, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/option.c
 */

#include "postgres_fe.h"

#ifdef WIN32
#include <io.h>
#endif

#include "common/string.h"
#include "fe_utils/option_utils.h"
#include "getopt_long.h"
#include "pg_upgrade.h"
#include "utils/pidfile.h"

static void usage(void);
static void check_required_directory(char **dirpath,
									 const char *envVarName, bool useCwd,
									 const char *cmdLineOption, const char *description,
									 bool missingOk);
#define FIX_DEFAULT_READ_ONLY "-c default_transaction_read_only=false"


UserOpts	user_opts;


/*
 * parseCommandLine()
 *
 *	Parses the command line (argc, argv[]) and loads structures
 */
void
parseCommandLine(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"old-datadir", required_argument, NULL, 'd'},
		{"new-datadir", required_argument, NULL, 'D'},
		{"old-bindir", required_argument, NULL, 'b'},
		{"new-bindir", required_argument, NULL, 'B'},
		{"no-sync", no_argument, NULL, 'N'},
		{"old-options", required_argument, NULL, 'o'},
		{"new-options", required_argument, NULL, 'O'},
		{"old-port", required_argument, NULL, 'p'},
		{"new-port", required_argument, NULL, 'P'},

		{"username", required_argument, NULL, 'U'},
		{"check", no_argument, NULL, 'c'},
		{"link", no_argument, NULL, 'k'},
		{"retain", no_argument, NULL, 'r'},
		{"jobs", required_argument, NULL, 'j'},
		{"socketdir", required_argument, NULL, 's'},
		{"verbose", no_argument, NULL, 'v'},
		{"clone", no_argument, NULL, 1},
		{"copy", no_argument, NULL, 2},
		{"copy-file-range", no_argument, NULL, 3},
		{"sync-method", required_argument, NULL, 4},
		{"no-statistics", no_argument, NULL, 5},
		{"set-char-signedness", required_argument, NULL, 6},
		{"swap", no_argument, NULL, 7},

		{NULL, 0, NULL, 0}
	};
	int			option;			/* Command line option */
	int			optindex = 0;	/* used by getopt_long */
	int			os_user_effective_id;
	DataDirSyncMethod unused;

	user_opts.do_sync = true;
	user_opts.transfer_mode = TRANSFER_MODE_COPY;
	user_opts.do_statistics = true;
	user_opts.char_signedness = -1;

	os_info.progname = get_progname(argv[0]);

	/* Process libpq env. variables; load values here for usage() output */
	old_cluster.port = getenv("PGPORTOLD") ? atoi(getenv("PGPORTOLD")) : DEF_PGUPORT;
	new_cluster.port = getenv("PGPORTNEW") ? atoi(getenv("PGPORTNEW")) : DEF_PGUPORT;

	os_user_effective_id = get_user_info(&os_info.user);
	/* we override just the database user name;  we got the OS id above */
	if (getenv("PGUSER"))
	{
		pg_free(os_info.user);
		/* must save value, getenv()'s pointer is not stable */
		os_info.user = pg_strdup(getenv("PGUSER"));
	}

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage();
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pg_upgrade (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}

	/* Allow help and version to be run as root, so do the test here. */
	if (os_user_effective_id == 0)
		pg_fatal("%s: cannot be run as root", os_info.progname);

	while ((option = getopt_long(argc, argv, "b:B:cd:D:j:kNo:O:p:P:rs:U:v",
								 long_options, &optindex)) != -1)
	{
		switch (option)
		{
			case 'b':
				old_cluster.bindir = pg_strdup(optarg);
				break;

			case 'B':
				new_cluster.bindir = pg_strdup(optarg);
				break;

			case 'c':
				user_opts.check = true;
				break;

			case 'd':
				old_cluster.pgdata = pg_strdup(optarg);
				break;

			case 'D':
				new_cluster.pgdata = pg_strdup(optarg);
				break;

			case 'j':
				user_opts.jobs = atoi(optarg);
				break;

			case 'k':
				user_opts.transfer_mode = TRANSFER_MODE_LINK;
				break;

			case 'N':
				user_opts.do_sync = false;
				break;

			case 'o':
				/* append option? */
				if (!old_cluster.pgopts)
					old_cluster.pgopts = pg_strdup(optarg);
				else
				{
					char	   *old_pgopts = old_cluster.pgopts;

					old_cluster.pgopts = psprintf("%s %s", old_pgopts, optarg);
					free(old_pgopts);
				}
				break;

			case 'O':
				/* append option? */
				if (!new_cluster.pgopts)
					new_cluster.pgopts = pg_strdup(optarg);
				else
				{
					char	   *new_pgopts = new_cluster.pgopts;

					new_cluster.pgopts = psprintf("%s %s", new_pgopts, optarg);
					free(new_pgopts);
				}
				break;

			case 'p':
				if ((old_cluster.port = atoi(optarg)) <= 0)
					pg_fatal("invalid old port number");
				break;

			case 'P':
				if ((new_cluster.port = atoi(optarg)) <= 0)
					pg_fatal("invalid new port number");
				break;

			case 'r':
				log_opts.retain = true;
				break;

			case 's':
				user_opts.socketdir = pg_strdup(optarg);
				break;

			case 'U':
				pg_free(os_info.user);
				os_info.user = pg_strdup(optarg);
				os_info.user_specified = true;
				break;

			case 'v':
				log_opts.verbose = true;
				break;

			case 1:
				user_opts.transfer_mode = TRANSFER_MODE_CLONE;
				break;

			case 2:
				user_opts.transfer_mode = TRANSFER_MODE_COPY;
				break;

			case 3:
				user_opts.transfer_mode = TRANSFER_MODE_COPY_FILE_RANGE;
				break;
			case 4:
				if (!parse_sync_method(optarg, &unused))
					exit(1);
				user_opts.sync_method = pg_strdup(optarg);
				break;

			case 5:
				user_opts.do_statistics = false;
				break;

			case 6:
				if (pg_strcasecmp(optarg, "signed") == 0)
					user_opts.char_signedness = 1;
				else if (pg_strcasecmp(optarg, "unsigned") == 0)
					user_opts.char_signedness = 0;
				else
					pg_fatal("invalid argument for option %s", "--set-char-signedness");
				break;

			case 7:
				user_opts.transfer_mode = TRANSFER_MODE_SWAP;
				break;

			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
						os_info.progname);
				exit(1);
		}
	}

	if (optind < argc)
		pg_fatal("too many command-line arguments (first is \"%s\")", argv[optind]);

	if (!user_opts.sync_method)
		user_opts.sync_method = pg_strdup("fsync");

	if (log_opts.verbose)
		pg_log(PG_REPORT, "Running in verbose mode");

	log_opts.isatty = isatty(fileno(stdout));

	/* Turn off read-only mode;  add prefix to PGOPTIONS? */
	if (getenv("PGOPTIONS"))
	{
		char	   *pgoptions = psprintf("%s %s", FIX_DEFAULT_READ_ONLY,
										 getenv("PGOPTIONS"));

		setenv("PGOPTIONS", pgoptions, 1);
		pfree(pgoptions);
	}
	else
		setenv("PGOPTIONS", FIX_DEFAULT_READ_ONLY, 1);

	/* Get values from env if not already set */
	check_required_directory(&old_cluster.bindir, "PGBINOLD", false,
							 "-b", _("old cluster binaries reside"), false);
	check_required_directory(&new_cluster.bindir, "PGBINNEW", false,
							 "-B", _("new cluster binaries reside"), true);
	check_required_directory(&old_cluster.pgdata, "PGDATAOLD", false,
							 "-d", _("old cluster data resides"), false);
	check_required_directory(&new_cluster.pgdata, "PGDATANEW", false,
							 "-D", _("new cluster data resides"), false);
	check_required_directory(&user_opts.socketdir, "PGSOCKETDIR", true,
							 "-s", _("sockets will be created"), false);

#ifdef WIN32

	/*
	 * On Windows, initdb --sync-only will fail with a "Permission denied"
	 * error on file pg_upgrade_utility.log if pg_upgrade is run inside the
	 * new cluster directory, so we do a check here.
	 */
	{
		char		cwd[MAXPGPATH],
					new_cluster_pgdata[MAXPGPATH];

		strlcpy(new_cluster_pgdata, new_cluster.pgdata, MAXPGPATH);
		canonicalize_path(new_cluster_pgdata);

		if (!getcwd(cwd, MAXPGPATH))
			pg_fatal("could not determine current directory");
		canonicalize_path(cwd);
		if (path_is_prefix_of_path(new_cluster_pgdata, cwd))
			pg_fatal("cannot run pg_upgrade from inside the new cluster data directory on Windows");
	}
#endif
}


static void
usage(void)
{
	printf(_("pg_upgrade upgrades a PostgreSQL cluster to a different major version.\n\n"));
	printf(_("Usage:\n"));
	printf(_("  pg_upgrade [OPTION]...\n\n"));
	printf(_("Options:\n"));
	printf(_("  -b, --old-bindir=BINDIR       old cluster executable directory\n"));
	printf(_("  -B, --new-bindir=BINDIR       new cluster executable directory (default\n"
			 "                                same directory as pg_upgrade)\n"));
	printf(_("  -c, --check                   check clusters only, don't change any data\n"));
	printf(_("  -d, --old-datadir=DATADIR     old cluster data directory\n"));
	printf(_("  -D, --new-datadir=DATADIR     new cluster data directory\n"));
	printf(_("  -j, --jobs=NUM                number of simultaneous processes or threads to use\n"));
	printf(_("  -k, --link                    link instead of copying files to new cluster\n"));
	printf(_("  -N, --no-sync                 do not wait for changes to be written safely to disk\n"));
	printf(_("  -o, --old-options=OPTIONS     old cluster options to pass to the server\n"));
	printf(_("  -O, --new-options=OPTIONS     new cluster options to pass to the server\n"));
	printf(_("  -p, --old-port=PORT           old cluster port number (default %d)\n"), old_cluster.port);
	printf(_("  -P, --new-port=PORT           new cluster port number (default %d)\n"), new_cluster.port);
	printf(_("  -r, --retain                  retain SQL and log files after success\n"));
	printf(_("  -s, --socketdir=DIR           socket directory to use (default current dir.)\n"));
	printf(_("  -U, --username=NAME           cluster superuser (default \"%s\")\n"), os_info.user);
	printf(_("  -v, --verbose                 enable verbose internal logging\n"));
	printf(_("  -V, --version                 display version information, then exit\n"));
	printf(_("  --clone                       clone instead of copying files to new cluster\n"));
	printf(_("  --copy                        copy files to new cluster (default)\n"));
	printf(_("  --copy-file-range             copy files to new cluster with copy_file_range\n"));
	printf(_("  --no-statistics               do not import statistics from old cluster\n"));
	printf(_("  --set-char-signedness=OPTION  set new cluster char signedness to \"signed\" or\n"
			 "                                \"unsigned\"\n"));
	printf(_("  --swap                        move data directories to new cluster\n"));
	printf(_("  --sync-method=METHOD          set method for syncing files to disk\n"));
	printf(_("  -?, --help                    show this help, then exit\n"));
	printf(_("\n"
			 "Before running pg_upgrade you must:\n"
			 "  create a new database cluster (using the new version of initdb)\n"
			 "  shutdown the postmaster servicing the old cluster\n"
			 "  shutdown the postmaster servicing the new cluster\n"));
	printf(_("\n"
			 "When you run pg_upgrade, you must provide the following information:\n"
			 "  the data directory for the old cluster  (-d DATADIR)\n"
			 "  the data directory for the new cluster  (-D DATADIR)\n"
			 "  the \"bin\" directory for the old version (-b BINDIR)\n"
			 "  the \"bin\" directory for the new version (-B BINDIR)\n"));
	printf(_("\n"
			 "For example:\n"
			 "  pg_upgrade -d oldCluster/data -D newCluster/data -b oldCluster/bin -B newCluster/bin\n"
			 "or\n"));
#ifndef WIN32
	printf(_("  $ export PGDATAOLD=oldCluster/data\n"
			 "  $ export PGDATANEW=newCluster/data\n"
			 "  $ export PGBINOLD=oldCluster/bin\n"
			 "  $ export PGBINNEW=newCluster/bin\n"
			 "  $ pg_upgrade\n"));
#else
	printf(_("  C:\\> set PGDATAOLD=oldCluster/data\n"
			 "  C:\\> set PGDATANEW=newCluster/data\n"
			 "  C:\\> set PGBINOLD=oldCluster/bin\n"
			 "  C:\\> set PGBINNEW=newCluster/bin\n"
			 "  C:\\> pg_upgrade\n"));
#endif
	printf(_("\nReport bugs to <%s>.\n"), PACKAGE_BUGREPORT);
	printf(_("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL);
}


/*
 * check_required_directory()
 *
 * Checks a directory option.
 *	dirpath		  - the directory name supplied on the command line, or NULL
 *	envVarName	  - the name of an environment variable to get if dirpath is NULL
 *	useCwd		  - true if OK to default to CWD
 *	cmdLineOption - the command line option for this directory
 *	description   - a description of this directory option
 *	missingOk	  - true if OK that both dirpath and envVarName are not existing
 *
 * We use the last two arguments to construct a meaningful error message if the
 * user hasn't provided the required directory name.
 */
static void
check_required_directory(char **dirpath, const char *envVarName, bool useCwd,
						 const char *cmdLineOption, const char *description,
						 bool missingOk)
{
	if (*dirpath == NULL || strlen(*dirpath) == 0)
	{
		const char *envVar;

		if ((envVar = getenv(envVarName)) && strlen(envVar))
			*dirpath = pg_strdup(envVar);
		else if (useCwd)
		{
			char		cwd[MAXPGPATH];

			if (!getcwd(cwd, MAXPGPATH))
				pg_fatal("could not determine current directory");
			*dirpath = pg_strdup(cwd);
		}
		else if (missingOk)
			return;
		else
			pg_fatal("You must identify the directory where the %s.\n"
					 "Please use the %s command-line option or the %s environment variable.",
					 description, cmdLineOption, envVarName);
	}

	/*
	 * Clean up the path, in particular trimming any trailing path separators,
	 * because we construct paths by appending to this path.
	 */
	canonicalize_path(*dirpath);
}

/*
 * adjust_data_dir
 *
 * If a configuration-only directory was specified, find the real data dir
 * by querying the running server.  This has limited checking because we
 * can't check for a running server because we can't find postmaster.pid.
 *
 * On entry, cluster->pgdata has been set from command line or env variable,
 * but cluster->pgconfig isn't set.  We fill both variables with corrected
 * values.
 */
void
adjust_data_dir(ClusterInfo *cluster)
{
	char		filename[MAXPGPATH];
	char		cmd[MAXPGPATH],
				cmd_output[MAX_STRING];
	FILE	   *fp,
			   *output;
	int			rc;

	/* Initially assume config dir and data dir are the same */
	cluster->pgconfig = pg_strdup(cluster->pgdata);

	/* If there is no postgresql.conf, it can't be a config-only dir */
	snprintf(filename, sizeof(filename), "%s/postgresql.conf", cluster->pgconfig);
	if ((fp = fopen(filename, "r")) == NULL)
		return;
	fclose(fp);

	/* If PG_VERSION exists, it can't be a config-only dir */
	snprintf(filename, sizeof(filename), "%s/PG_VERSION", cluster->pgconfig);
	if ((fp = fopen(filename, "r")) != NULL)
	{
		fclose(fp);
		return;
	}

	/* Must be a configuration directory, so find the real data directory. */

	if (cluster == &old_cluster)
		prep_status("Finding the real data directory for the source cluster");
	else
		prep_status("Finding the real data directory for the target cluster");

	/*
	 * We don't have a data directory yet, so we can't check the PG version,
	 * so this might fail --- only works for PG 9.2+.   If this fails,
	 * pg_upgrade will fail anyway because the data files will not be found.
	 */
	snprintf(cmd, sizeof(cmd), "\"%s/postgres\" -D \"%s\" -C data_directory",
			 cluster->bindir, cluster->pgconfig);
	fflush(NULL);

	if ((output = popen(cmd, "r")) == NULL ||
		fgets(cmd_output, sizeof(cmd_output), output) == NULL)
		pg_fatal("could not get data directory using %s: %m", cmd);

	rc = pclose(output);
	if (rc != 0)
		pg_fatal("could not get data directory using %s: %s",
				 cmd, wait_result_to_str(rc));

	/* strip trailing newline and carriage return */
	(void) pg_strip_crlf(cmd_output);

	cluster->pgdata = pg_strdup(cmd_output);

	check_ok();
}


/*
 * get_sock_dir
 *
 * Identify the socket directory to use for this cluster.  If we're doing
 * a live check (old cluster only), we need to find out where the postmaster
 * is listening.  Otherwise, we're going to put the socket into the current
 * directory.
 */
void
get_sock_dir(ClusterInfo *cluster)
{
#if !defined(WIN32)
	if (!user_opts.live_check || cluster == &new_cluster)
		cluster->sockdir = user_opts.socketdir;
	else
	{
		/*
		 * If we are doing a live check, we will use the old cluster's Unix
		 * domain socket directory so we can connect to the live server.
		 */
		unsigned short orig_port = cluster->port;
		char		filename[MAXPGPATH],
					line[MAXPGPATH];
		FILE	   *fp;
		int			lineno;

		snprintf(filename, sizeof(filename), "%s/postmaster.pid",
				 cluster->pgdata);
		if ((fp = fopen(filename, "r")) == NULL)
			pg_fatal("could not open file \"%s\": %m", filename);

		for (lineno = 1;
			 lineno <= Max(LOCK_FILE_LINE_PORT, LOCK_FILE_LINE_SOCKET_DIR);
			 lineno++)
		{
			if (fgets(line, sizeof(line), fp) == NULL)
				pg_fatal("could not read line %d from file \"%s\": %m",
						 lineno, filename);

			/* potentially overwrite user-supplied value */
			if (lineno == LOCK_FILE_LINE_PORT)
				sscanf(line, "%hu", &old_cluster.port);
			if (lineno == LOCK_FILE_LINE_SOCKET_DIR)
			{
				/* strip trailing newline and carriage return */
				cluster->sockdir = pg_strdup(line);
				(void) pg_strip_crlf(cluster->sockdir);
			}
		}
		fclose(fp);

		/* warn of port number correction */
		if (orig_port != DEF_PGUPORT && old_cluster.port != orig_port)
			pg_log(PG_WARNING, "user-supplied old port number %hu corrected to %hu",
				   orig_port, cluster->port);
	}
#else							/* WIN32 */
	cluster->sockdir = NULL;
#endif
}
