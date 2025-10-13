// SPDX-License-Identifier: GPL-2.0
/*
 * XSC network operation handlers
 */

#include <linux/net.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <net/sock.h>
#include "xsc_uapi.h"

int xsc_dispatch_net(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	struct socket *sock;
	int ret;

	switch (sqe->opcode) {
	case XSC_OP_SOCKET:
		return __sys_socket(sqe->fd, sqe->len, sqe->off);

	case XSC_OP_BIND: {
		struct sockaddr __user *addr = (struct sockaddr __user *)sqe->addr;
		return __sys_bind(sqe->fd, addr, sqe->len);
	}

	case XSC_OP_LISTEN:
		return __sys_listen(sqe->fd, sqe->len);

	case XSC_OP_ACCEPT: {
		struct sockaddr __user *addr = (struct sockaddr __user *)sqe->addr;
		int __user *addrlen = (int __user *)sqe->addr2;
		return __sys_accept4(sqe->fd, addr, addrlen, sqe->accept_flags);
	}

	case XSC_OP_CONNECT: {
		struct sockaddr __user *addr = (struct sockaddr __user *)sqe->addr;
		return __sys_connect(sqe->fd, addr, sqe->len);
	}

	case XSC_OP_SENDTO: {
		void __user *buf = (void __user *)sqe->addr;
		struct sockaddr __user *addr = (struct sockaddr __user *)sqe->addr2;
		return __sys_sendto(sqe->fd, buf, sqe->len, sqe->msg_flags, addr, sqe->off);
	}

	case XSC_OP_RECVFROM: {
		void __user *buf = (void __user *)sqe->addr;
		struct sockaddr __user *addr = (struct sockaddr __user *)sqe->addr2;
		int __user *addrlen = (int __user *)(sqe->addr2 + sizeof(struct sockaddr_storage));
		return __sys_recvfrom(sqe->fd, buf, sqe->len, sqe->msg_flags, addr, addrlen);
	}

	default:
		return -EINVAL;
	}
}
