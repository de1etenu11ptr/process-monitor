#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#define BUFFER_SIZE 8192

/* wrapper to send standard subscription messages to the connector */
struct cn_subscribe_msg {
	struct nlmsghdr nl_hdr;
	struct cn_msg cn_msg;
	enum proc_cn_mcast_op cn_mcast;
};

struct cn_subscribe_msg *get_subscribe_msg(struct cn_subscribe_msg *sub)
{
	sub->nl_hdr.nlmsg_len = sizeof(struct cn_subscribe_msg);
	sub->nl_hdr.nlmsg_pid = getpid();
	sub->nl_hdr.nlmsg_type = NLMSG_DONE;

	sub->cn_msg.id.idx = CN_IDX_PROC;
	sub->cn_msg.id.val = CN_VAL_PROC;
	sub->cn_msg.len = sizeof(enum proc_cn_mcast_op);

	/* tells the kernel/proc connector that we would like to start
	 * receiving notification messages */
	/* PROC_CN_MCAST_IGNORE is used to unsubscribe */
	sub->cn_mcast = PROC_CN_MCAST_LISTEN;
	return sub;
}

int send_proc_op(int fd, struct cn_subscribe_msg *sub, enum proc_cn_mcast_op op)
{
	sub->cn_mcast = op;
	if (send(fd, sub, sizeof(*sub), 0) < 0) {
		perror("proc request failed");
		close(fd);
		return 0;
	}
	return 1;
}

int main()
{
	int fd;
	struct sockaddr_nl addr;
	char buffer[BUFFER_SIZE];

	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (fd < 0) {
		perror("socket creation failed (are you root?)");
		return EXIT_FAILURE;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid();
	/* core process mutations */
	addr.nl_groups = CN_IDX_PROC;

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "failed to bind group %d to fd %d\n%s\n",
			addr.nl_groups, fd, strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}

	struct cn_subscribe_msg *sub = malloc(sizeof(struct cn_subscribe_msg));
	get_subscribe_msg(sub);
	/* explicit subscription packet to activate the stream */
	if (!send_proc_op(fd, sub, PROC_CN_MCAST_LISTEN)) {
		close(fd);
		free(sub);
		return EXIT_FAILURE;
	}

	printf("monitoring system process mutations\n");

	while (1) {
		ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
		if (len < 0) {
			perror("failed reading netlink payload");
			break;
		}

		struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;

		while (NLMSG_OK(nlh, len)) {
			if (nlh->nlmsg_type == NLMSG_ERROR) {
				break;
			}

			/* connector metadata */
			struct cn_msg *cn_m = (struct cn_msg *)NLMSG_DATA(nlh);

			/* event payload */
			struct proc_event *ev = (struct proc_event *)cn_m->data;

			switch (ev->what) {
			case PROC_EVENT_FORK:
				printf("[FORK] parent pid: %d -> child pid: %d\n",
				       ev->event_data.fork.parent_pid,
				       ev->event_data.fork.child_pid);
				break;
			case PROC_EVENT_EXEC:
				printf("[EXEC] process id: %d executed a binary.\n",
				       ev->event_data.exec.process_pid);
				break;
			case PROC_EVENT_EXIT:
				printf("[EXIT] process id: %d finished (exit code: %d)\n",
				       ev->event_data.exit.process_pid,
				       ev->event_data.exit.exit_code);
				break;
			default:
				break;
			}

			nlh = NLMSG_NEXT(nlh, len);
		}
	}

	send_proc_op(fd, sub, PROC_CN_MCAST_IGNORE);
	close(fd);
	free(sub);
	return 0;
}
