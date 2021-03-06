#include <stdio.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pty.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <array>
#include <sys/sysmacros.h>
#include <sys/epoll.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <vector>
#include <functional>
#include <android/log.h>
#include <termios.h>

#define MSG_EVENT_MAGIC	0x1234 /* replace with action event magic */
#define MSG_CODE_OFFSET		0 /* XXX: replace with actual interested msg1 code offset */
#define MSG_ACTION_OFFSET	0 /* XXX: replace with actual interested msg2 code offset */
#define TTY_PATH "/dev/REPLACE_WITH_YOUR_ORIGNINAL_TTY_DEVICE"
#define TTY_PATH_BAK	"/dev/REPLACE_WITH_YOUR_MODIFIED_TTY_DEVICE"
#define MAJOR	12 /*XXX: replace with actual tty device node major number */
#define MINOR	34 /*XXX: replace with actual tty device node minor number */

#define LOG_TAG "ttymux"

#define		LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define		LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define 	LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x[0])))

enum {
	TRIGGER_UP = 0, /* default */
	TRIGGER_DOWN,
	TRIGGER_LONG,
};

struct MsgOperator {
	int msgCode;
	int msgAction;
	int count;
	int trigger;
	const char cmd[256];
};

/* XXX: replace with your actual msg code/action and commands */
static MsgOperator gOper[] = {
	{
		.msgCode = 1,
		.msgAction = 4,
		.cmd = "/system/bin/logwrapper echo SOME_MSG", /* previous */
	},

};

static int handleMsgEvent(int msgCode, int msgAction) {
	static int lastLongMsgIndex = -1;

	if (lastLongMsgIndex >= 0) {
		/* ignore long press second time */
		if ((gOper[lastLongMsgIndex].msgCode == msgCode) && (gOper[lastLongMsgIndex].msgAction == msgAction)) {
			gOper[lastLongMsgIndex].count++;
			return 0;
		} else { /* not same msg, restore last long pressed msg event */
			gOper[lastLongMsgIndex].count = 0;
			lastLongMsgIndex = -1;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(gOper); i++) {
		if ((msgCode != gOper[i].msgCode) || (msgAction != gOper[i].msgAction)) {
			continue;
		}

		/* msg matched */
		if (gOper[i].trigger != TRIGGER_LONG) {
			#ifdef USE_FORK
			if (fork() == 0) { /* normal handling */
				LOGE("executing %s", gOper[i].cmd);
				system(gOper[i].cmd);
				exit(0);
			}
			#else
			system(gOper[i].cmd);
			#endif
		} else {
			if (gOper[i].count <= 0) { /* not ever pressed */
			#ifdef USE_FORK
				if (fork() == 0) {
					LOGE("executing %s", gOper[i].cmd);
					system(gOper[i].cmd);
					exit(0);
				} else {
					gOper[i].count++;
				}
			#else
				system(gOper[i].cmd);
				gOper[i].count++;

			#endif

				/* record it */
				lastLongMsgIndex = i;
			}
		}
		return 0;
	}
	return -1;
}

static int handleBufferReady(const char *buffer, int count, std::function<int(int, int)> onMsg) {
	for (int i = 0; i < count; i++) {
		LOGI("%x ", buffer[i]);
	}
	LOGI("\n");

#define MSG_EVENT_SIZE	9
	if (count < MSG_EVENT_SIZE) {
		LOGE("incorrect msg event size");
		return -1;
	}

	uint32_t magic = *(uint32_t*)buffer;
	if (magic != MSG_EVENT_MAGIC) {
		LOGE("msg event magic number not match\n");
		return -1;
	}

	LOGI("#########msgCode:%d action:%d#########\n", buffer[MSG_CODE_OFFSET], buffer[MSG_ACTION_OFFSET]);

	return onMsg(buffer[MSG_CODE_OFFSET], buffer[MSG_ACTION_OFFSET]);
}

typedef void (*mysig_t)(int);
int openpty(int *amaster, int *aslave, char *name, struct termios *termp, struct winsize *winp) {
	/*
	 * This code is used e.g. on Solaris 2.x.  (Note that Solaris 2.3
	 * also has bsd-style ptys, but they simply do not work.)
	 */
	int ptm;
	char *pts;
	mysig_t old_signal;

	if ((ptm = open("/dev/ptmx", O_RDWR | O_NOCTTY)) == -1)
		return (-1);

	/* XXX: need to close ptm on error? */
	old_signal = signal(SIGCHLD, SIG_DFL);
	if (grantpt(ptm) < 0)
		return (-1);
	signal(SIGCHLD, old_signal);

	if (unlockpt(ptm) < 0)
		return (-1);

	if ((pts = ptsname(ptm)) == NULL) {
		LOGE("pts name returns null\n");
		return -1;
	}
	LOGI("pts name returns %s\n", pts);
	strcpy(name, pts);
	*amaster = ptm;

	/* Open the slave side. */
	if ((*aslave = open(pts, O_RDWR | O_NOCTTY)) == -1) {
		close(*amaster);
		return (-1);
	}
	return 0;
}

int main() {

	unlink(TTY_PATH_BAK);

	struct stat st;
	int rc = stat(TTY_PATH, &st);
	if (((rc == 0) && (S_ISLNK(st.st_mode))) || ((rc < 0) && (errno == ENOENT))) {
		unlink(TTY_PATH);
		if (mknod(TTY_PATH, S_IFCHR | 0666, makedev(MAJOR, MINOR)) < 0) {
			LOGE("mknod failed(%s)\n", strerror(errno));
			exit(-1);
		}
	}

	int mcuFd = open(TTY_PATH, O_RDWR | O_NONBLOCK);
	if (mcuFd < 0) {
		LOGE("open %s failed(%s)\n", TTY_PATH, strerror(errno));
		return -1;
	}

	LOGI("open %s success\n", TTY_PATH);
	struct termios ios = {
		.c_iflag = 0x500,
		.c_oflag = 0x5,
		.c_cflag = 0x1cb2,
		.c_lflag = 0x8a3b,
		.c_line = 0,
		.c_cc = {0x03, 0x1c, 0x7f, 0x15, 0x04, 0x00, 0x01, 0x00, 0x11, 0x13, 0x1a, 0x00, 0x12, 0x0f, 0x17, 0x16, 0x00, 0x00, 0x00},
	};

	ioctl(mcuFd, TCFLSH, 2);

	if (tcsetattr(mcuFd, TCSANOW, &ios) < 0) {
		LOGE("set termio failed(%s)", strerror(errno));
		exit(-1);
	}

	ioctl(mcuFd, TCFLSH, 2);

	ios.c_iflag = 0x10;
	ios.c_oflag = 0x04;
	ios.c_lflag = 0x8a20;

	if (tcsetattr(mcuFd, TCSANOW, &ios) < 0) {
		LOGE("set termio 2 failed(%s)", strerror(errno));
		exit(-1);
	}

	/* rename old file */
	if (rename(TTY_PATH, TTY_PATH_BAK) < 0) {
		LOGE("rename %s failed(%s)\n", TTY_PATH, strerror(errno));
		exit(-1);
	}

	int masterFd, slaveFd;
	char slaveName[128];

	if (openpty(&masterFd, &slaveFd, slaveName, NULL, NULL) < 0) {
		LOGE("open pty failed(%s)\n", strerror(errno));
		exit(-1);
	}
	//close(slaveFd);
	fcntl(masterFd, F_SETFL, O_NONBLOCK);
	LOGI("pty device opened. master:%d slave:%d name:%s\n", masterFd, slaveFd, slaveName);

	if (symlink(slaveName, TTY_PATH) < 0) {
		LOGE("create symlink failed(%s)\n", strerror(errno));
		exit(-1);
	}

	if (chmod(slaveName, 0666) < 0) {
		LOGE("chmod %s failed(%s)\n", slaveName, strerror(errno));
		exit(-1);
	}

	/* create listen fd */
	int listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd < 0) {
		LOGE("create socket failed(%s)\n", strerror(errno));
		exit(-1);
	}

	int on = 1;
	if (setsockopt(listenFd, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on)) < 0) {
		LOGE("set reuse addr failed(%s)\n", strerror(errno));
		exit(-1);
	}

	if (ioctl(listenFd, FIONBIO, (char *)&on) < 0) {
		LOGE("set non blocking failed(%s)\n", strerror(errno));
		exit(-1);
	}

	struct sockaddr_in sockAddr;
	memset(&sockAddr, 0, sizeof(sockAddr));
	
#define LISTEN_PORT	5858

	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sockAddr.sin_port = htons(LISTEN_PORT);

	if (bind(listenFd, (struct sockaddr*)&sockAddr , sizeof(sockAddr)) < 0) {
		LOGE("bind address failed(%s)\n", strerror(errno));
		exit(-1);
	}

#define MAX_CLIENTS	2
	if (listen(listenFd, MAX_CLIENTS) < 0) {
		LOGE("listen address failed(%s)\n", strerror(errno));
		exit(-1);
	}

	/* create epoll fd */
	int epollFd = epoll_create(1);
	if (epollFd < 0) {
		LOGE("create epoll failed(%s)\n", strerror(errno));
		exit(-1);
	}

#define MAX_POLL_FD 10
	int nfds = 0;
    struct epoll_event ev, events[MAX_POLL_FD];

	/* add all pollfds here. Now we only have one */
	ev.data.fd = mcuFd;
	ev.events = EPOLLIN;
	epoll_ctl(epollFd, EPOLL_CTL_ADD, mcuFd, &ev);

	/* add pty master */
	ev.data.fd = masterFd;
	ev.events = EPOLLIN;
	epoll_ctl(epollFd, EPOLL_CTL_ADD, masterFd, &ev);

	/* add listen fd */
	ev.data.fd = listenFd;
	ev.events = EPOLLIN;
	epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &ev);

#define MAX_BUFFER_SIZE	1024
	char buffer[MAX_BUFFER_SIZE];

	std::vector<int> clientFds;

	/* looooop */
	while (1) {
		 nfds = epoll_wait(epollFd, events, MAX_POLL_FD, -1);
		if (nfds < 0) {
			LOGE("poll failed(%d) (%s)\n", errno, strerror(errno));
			/* FIXME: error handling */
			if (errno == EINTR) {
				continue;
			}
			exit(-1);
		}

		for (int i = 0; i < nfds; i++) {
			if (!((events[i].events & EPOLLIN) || (events[i].events) & EPOLLHUP)) { /* FIXME: error handling */
				LOGE("unexpected poll events");
				exit(-1);
			}

			if (events[i].data.fd == masterFd) { /* acore write something */
				int len;
				while ((len = read(events[i].data.fd, buffer, sizeof(buffer))) > 0) {
					/* echo back to mcu fd */
					write(mcuFd, buffer, len);
				}

				/* EAGAIN */
				if (errno != EAGAIN) {
					LOGE("something unexpected happen for masterFd(%s), exiting\n",
						strerror(errno));
					exit(-1);
				}

			} else if (events[i].data.fd == mcuFd) { /* mcu to android events */
				std::string msgBuff;
				int len;
				while ((len = read(events[i].data.fd, buffer, sizeof(buffer))) > 0) {
					/* echo back to mcu fd */
					msgBuff.append(buffer, len);
				}
				if (len < 0) {
					if (errno != EAGAIN) {
						LOGE("read buffer from mcuFd error(%s)\n", strerror(errno));
						exit(-1);
					}
				}

				LOGI("read %zd bytes\n", msgBuff.size());

				if ((handleBufferReady(msgBuff.c_str(), msgBuff.size(),
					[&](int msgCode, int msgAction) {
						/* handle it myself */
						if (clientFds.size() <= 0) {
							return handleMsgEvent(msgCode, msgAction);
						}

						/* loop to send data, mark disconnected fds and remove it */
						for (auto iter = clientFds.begin(); iter != clientFds.end(); iter++) {
							std::string msgEvent = std::to_string(msgCode) + "," + std::to_string(msgAction) + ";";
							/* error handling */
							if (write(*iter, msgEvent.c_str(), msgEvent.size()) < 0) {
								if (errno == ECONNRESET) {
									/* delete this fd from epoll */

									epoll_ctl(epollFd, EPOLL_CTL_DEL, *iter, NULL);

									*iter = -1;
								}
							}


						}

						for (auto iter = clientFds.begin(); iter != clientFds.end();) {
							if (*iter < 0) {
								clientFds.erase(iter);
							} else {
								iter++;
							}
						}

						return clientFds.size() > 0 ? 0:-1;
					}) < 0)) { /* msg not handled or no android clients intrested in */
					/* XXX: hard code here. Should pass this value to pts device */
					int len = write(masterFd, msgBuff.c_str(), msgBuff.size());
					LOGI("write %d bytes back to hsae core \n", len);
				}
			} else if (events[i].data.fd == listenFd) { /* accept clients */
				/* not a actual server, so don't need handle race conditions */
				int newFd = accept(listenFd, NULL, NULL);
				if (newFd < 0) {
					LOGE("accept failed(%s)\n", strerror(errno));
					exit(-1);
				}

				/* set to non-blocking */
				fcntl(masterFd, F_SETFL, O_NONBLOCK);

				LOGI("new connection established(%d)\n", newFd);
				/* add all pollfds here. Now we only have one */
				ev.data.fd = newFd;
				ev.events = EPOLLIN;
				epoll_ctl(epollFd, EPOLL_CTL_ADD, newFd, &ev);

				clientFds.push_back(newFd);
			} else { /* handle client fd */
				if (clientFds.size() <= 0) {
					LOGE("unexpected fd:%d", events[i].data.fd);
					continue;
				}

				bool shouldClose = false;
				auto iter = clientFds.begin();
				for (; iter != clientFds.end(); iter++) {
					if (*iter != events[i].data.fd)
						continue;
						/* poll in or poll hup ?? */

					if (events[i].events & EPOLLIN) {
						std::string ctlStr;
						int len;
						while ((len = read(events[i].data.fd, buffer, sizeof(buffer))) > 0) {
							/* echo back to mcu fd */
							ctlStr.append(buffer, len);
						}

						if (len == 0)
							shouldClose = true;

						LOGI("received control string from client %d:%s len:%d error(%s):0x%x", events[i].data.fd,
							ctlStr.c_str(), len, strerror(errno), events[i].events);

					}

					if (shouldClose || (events[i].events & EPOLLHUP) || (events[i].events & EPOLLERR)) {
						close(*iter);
						epoll_ctl(epollFd, EPOLL_CTL_DEL, *iter, NULL);

						*iter = -1;
						break;
					}  

					break;
				}

				if (*iter == -1) {
					clientFds.erase(iter);
				}
			}
		}
	}

	exit(-1);
	return 0;
}
 