// SPDX-License-Identifier: WTFPL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern char *optarg;

static int dport = 515;
static int sport = 731;	/* 721-731 */
static int job = -1;
static char *queue = NULL;
static char *file = NULL;
static int debug = 0;
static int linger = 0;

#define BUFSIZE 1024
static unsigned char buf[BUFSIZE];

#define HOST_FMT	".31s"
#define JOB_FMT		".15s"
#define FILE_FMT	".131s"

static int recv_response(int d)
{
	unsigned char rsp;

	return (read(d, &rsp, sizeof(rsp)) >= sizeof(rsp)) ? rsp : -1;
}

static int send_cmd(int d, char *cmdstr)
{
	int cmdlen = strlen(cmdstr);

	return (write(d, cmdstr, cmdlen) < cmdlen) ? -1 : 0;
}

static int send_byte(int d, int byte)
{
	char c = byte;

	return (write(d, &c, sizeof(c)) >= sizeof(c)) ? 0 : -1;
}

static int send_command2(int d)
{
	int rv = -1;

	sprintf(buf, "\x02%"JOB_FMT"\x0a", queue);
	if (debug)
		fprintf(stderr, "send_command2: %s", buf + 1);

	if (send_cmd(d, buf)) {
		fprintf(stderr, "send_command2: send_cmd\n");
		goto fin0;
	}
	if (recv_response(d)) {
		fprintf(stderr, "send_command2: recv NAK\n");
		goto fin0;
	}

	rv = 0;
fin0:
	return rv;
}

static int send_subcommand2(int d, char *host, int job)
{
	int n, rv = -1;
	char ctrl[BUFSIZE];

	n = 0;
	n += sprintf(ctrl + n, "H%"HOST_FMT"\x0a", host);
	n += sprintf(ctrl + n, "P%"HOST_FMT"\x0a", getlogin());
	n += sprintf(ctrl + n, "ldfA%03d%"JOB_FMT"\x0a", job, host);
	n += sprintf(ctrl + n, "UdfA%03d%"JOB_FMT"\x0a", job, host);
	n += sprintf(ctrl + n, "N%"FILE_FMT"\x0a", file);

	sprintf(buf, "\x02%d cfA%03d%"JOB_FMT"\x0a", n, job, host);
	if (debug) {
		fprintf(stderr, "send_subcommand2: %s", buf + 1);
		fprintf(stderr, "%s", ctrl);
	}

	if (send_cmd(d, buf)) {
		fprintf(stderr, "send_subcommand2: send_cmd\n");
		goto fin0;
	}
	if (recv_response(d)) {
		fprintf(stderr, "send_subcommand2: recv NAK\n");
		goto fin0;
	}

	if (write(d, ctrl, n) < n) {
		fprintf(stderr, "send_subcommand2: write (ctrl)\n");
		goto fin0;
	}
	if (send_byte(d, 0)) {
		fprintf(stderr, "send_subcommand2: send_byte (0)\n");
		goto fin0;
	}
	if (recv_response(d)) {
		fprintf(stderr, "send_subcommand2: recv NAK (ctrl)\n");
		goto fin0;
	}

	rv = 0;
fin0:
	return rv;
}

static int send_subcommand3(int d, char *host, int job)
{
	int size, rv = -1;
	unsigned int pos, remain;
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL) {
		fprintf(stderr, "send_subcommand3: fopen\n");
		goto fin0;
	}

	/* max 2Gbytes */
	fseek(fp, 0, SEEK_END);
	if ((size = ftell(fp)) <= 0) {
		fprintf(stderr, "send_subcommand3: unsupported file size\n");
		goto fin1;
	}
	rewind(fp);

	sprintf(buf, "\x03%d dfA%03d%"JOB_FMT"\x0a", size, job, host);
	fprintf(stderr, "send %d bytes, %s", size, strchr(buf, 'A') + 1);

	if (debug)
		fprintf(stderr, "send_subcommand3: %s", buf + 1);

	if (send_cmd(d, buf)) {
		fprintf(stderr, "send_subcommand3: send_cmd\n");
		goto fin1;
	}
	if (recv_response(d)) {
		fprintf(stderr, "send_subcommand3: recv NAK\n");
		goto fin1;
	}

	for (pos = 0; pos < size; pos += BUFSIZE) {
		remain = size - pos;
		if (remain > BUFSIZE) remain = BUFSIZE;

		if (fread(buf, remain, 1, fp) < 1) {
			fprintf(stderr, "send_subcommand3: fread\n");
			goto fin1;
		}
		if (write(d, buf, remain) < remain) {
			fprintf(stderr, "send_subcommand3: write (data)\n");
			goto fin1;
		}
		if (debug)
			fputc('.', stderr);
	}
	if (debug)
		fputc('\n', stderr);
	if (send_byte(d, 0)) {
		fprintf(stderr, "send_subcommand3: send_byte (0)\n");
		goto fin1;
	}
	if (recv_response(d)) {
		fprintf(stderr, "send_subcommand3: recv NAK (data)\n");
		goto fin1;
	}

	rv = 0;
fin1:
	fclose(fp);
fin0:
	return rv;
}

static int do_transfer(int d)
{
	int rv = -1;
	char host[1024];

	if (gethostname(host, sizeof(host)) < 0) {
		fprintf(stderr, "do_transfer: gethostname\n");
		goto fin0;
	}

	if (job < 0)
		job = arc4random();
	job = (unsigned int)job % 1000;

	if (send_command2(d) ||
	    send_subcommand2(d, host, job) || send_subcommand3(d, host, job))
		goto fin0;

	rv = 0;
fin0:
	return rv;
}

static int do_main(char *ipstr)
{
	int fd, en = 1, rv = -1;
	struct sockaddr_in saddr, daddr;
	struct linger l = {	/* avoid TCP/IP TIME_WAIT */
		.l_onoff = linger,
		.l_linger = 0,
	};

	/* create socket */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "do_main: socket\n");
		goto fin0;
	}
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &en, sizeof(en));
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));

	/* set port number (client side), if needed */
	if (sport) {
		memset(&saddr, 0, sizeof(saddr));
		saddr.sin_family = AF_INET;
		saddr.sin_addr.s_addr = INADDR_ANY;
		saddr.sin_port = htons(sport);

		if (bind(fd, (struct sockaddr *)&saddr,
			 sizeof(saddr)) < 0) {
			fprintf(stderr, "do_main: bind\n");
			goto fin1;
		}
	}

	/* connect to server */
	memset(&daddr, 0, sizeof(daddr));
	daddr.sin_family = AF_INET;
	daddr.sin_addr.s_addr = inet_addr(ipstr);
	daddr.sin_port = htons(dport);

	if (connect(fd, (struct sockaddr *)&daddr, sizeof(daddr)) < 0) {
		fprintf(stderr, "do_main: connect\n");
		goto fin1;
	}

	rv = do_transfer(fd);

fin1:
	close(fd);
fin0:
	return rv;
}

int main(int argc, char *argv[])
{
	int ch, help = 0;
	char *ipstr = NULL;
	char *appname = argv[0];

	while ((ch = getopt(argc, argv, "p:P:a:q:f:j:dRh")) != -1) {
		switch (ch) {
		case 'p':
			dport = atoi(optarg);
			break;
		case 'P':
			sport = atoi(optarg);
			break;
		case 'a':
			ipstr = optarg;
			break;
		case 'q':
			queue = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case 'j':
			job = atoi(optarg);
			break;
		case 'd':
			debug = 1;
			break;
		case 'R':
			linger = 1;
			break;
		case 'h':
		default:
			help = 1;
			break;
		}
	}

	if (help || ipstr == NULL || queue == NULL || file == NULL) {
		fprintf(stderr, "usage: %s -a [ip address(dest)] "
			"-q [queue] -f [filename]\n", appname);
		return -1;
	}
	return do_main(ipstr);
}
