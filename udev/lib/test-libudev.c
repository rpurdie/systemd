/*
 * test-libudev
 *
 * Copyright (C) 2008 Kay Sievers <kay.sievers@vrfy.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/select.h>

#include "libudev.h"

static void log_fn(struct udev *udev,
		   int priority, const char *file, int line, const char *fn,
		   const char *format, va_list args)
{
	printf("test-libudev: %s %s:%d ", fn, file, line);
	vprintf(format, args);
}

static void print_device(struct udev_device *device)
{
	const char *str;
	dev_t devnum;
	int count;
	struct udev_list *list;

	printf("*** device: %p ***\n", device);
	str = udev_device_get_action(device);
	printf("action:    '%s'\n", str);
	str = udev_device_get_syspath(device);
	printf("syspath:   '%s'\n", str);
	str = udev_device_get_devpath(device);
	printf("devpath:   '%s'\n", str);
	str = udev_device_get_subsystem(device);
	printf("subsystem: '%s'\n", str);
	str = udev_device_get_driver(device);
	printf("driver:    '%s'\n", str);
	str = udev_device_get_devnode(device);
	printf("devname:   '%s'\n", str);
	devnum = udev_device_get_devnum(device);
	printf("devnum:    %u:%u\n", major(devnum), minor(devnum));

	count = 0;
	list = udev_device_get_devlinks_list(device);
	while (list != NULL) {
		printf("link:      '%s'\n", udev_list_entry_get_name(list));
		count++;
		list = udev_list_entry_get_next(list);
	}
	printf("found %i links\n", count);

	count = 0;
	list = udev_device_get_properties_list(device);
	while (list != NULL) {
		printf("property:  '%s=%s'\n", udev_list_entry_get_name(list), udev_list_entry_get_value(list));
		count++;
		list = udev_list_entry_get_next(list);
	}
	printf("found %i properties\n", count);

	str = udev_device_get_attr_value(device, "dev");
	printf("attr{dev}: '%s'\n", str);

	printf("\n");
}

static int test_device(struct udev *udev, const char *syspath)
{
	struct udev_device *device;

	printf("looking at device: %s\n", syspath);
	device = udev_device_new_from_syspath(udev, syspath);
	if (device == NULL) {
		printf("no device\n");
		return -1;
	}
	print_device(device);
	udev_device_unref(device);
	return 0;
}

static int test_device_parents(struct udev *udev, const char *syspath)
{
	struct udev_device *device;
	struct udev_device *device_parent;

	printf("looking at device: %s\n", syspath);
	device = udev_device_new_from_syspath(udev, syspath);
	if (device == NULL)
		return -1;

	printf("looking at parents\n");
	device_parent = device;
	do {
		print_device(device_parent);
		device_parent = udev_device_get_parent(device_parent);
	} while (device_parent != NULL);

	printf("looking at parents again\n");
	device_parent = device;
	do {
		print_device(device_parent);
		device_parent = udev_device_get_parent(device_parent);
	} while (device_parent != NULL);
	udev_device_unref(device);

	return 0;
}

static int test_device_devnum(struct udev *udev)
{
	dev_t devnum = makedev(1, 3);
	struct udev_device *device;

	printf("looking up device: %u:%u\n", major(devnum), minor(devnum));
	device = udev_device_new_from_devnum(udev, 'c', devnum);
	if (device == NULL)
		return -1;
	print_device(device);
	udev_device_unref(device);
	return 0;
}

static int test_enumerate(struct udev *udev, const char *subsystem)
{
	struct udev_enumerate *enumerate;
	struct udev_list *list;
	int count = 0;

	enumerate = udev_enumerate_new_from_subsystems(udev, NULL);
	if (enumerate == NULL)
		return -1;
	list = udev_enumerate_get_list(enumerate);
	while (list != NULL) {
		struct udev_device *device;

		device = udev_device_new_from_syspath(udev, udev_list_entry_get_name(list));
		if (device != NULL) {
			printf("device:    '%s' (%s) '%s'\n",
			       udev_device_get_syspath(device),
			       udev_device_get_subsystem(device),
			       udev_device_get_sysname(device));
			udev_device_unref(device);
			count++;
		}
		list = udev_list_entry_get_next(list);
	}
	udev_enumerate_unref(enumerate);
	printf("found %i devices\n\n", count);
	return count;
}

static int test_monitor(struct udev *udev, const char *socket_path)
{
	struct udev_monitor *udev_monitor;
	fd_set readfds;
	int fd;

	udev_monitor = udev_monitor_new_from_socket(udev, socket_path);
	if (udev_monitor == NULL) {
		printf("no socket\n");
		return -1;
	}
	if (udev_monitor_enable_receiving(udev_monitor) < 0) {
		printf("bind failed\n");
		return -1;
	}

	fd = udev_monitor_get_fd(udev_monitor);
	FD_ZERO(&readfds);

	while (1) {
		struct udev_device *device;
		int fdcount;

		FD_SET(STDIN_FILENO, &readfds);
		FD_SET(fd, &readfds);

		printf("waiting for events on %s, press ENTER to exit\n", socket_path);
		fdcount = select(fd+1, &readfds, NULL, NULL, NULL);
		printf("select fd count: %i\n", fdcount);

		if (FD_ISSET(fd, &readfds)) {
			device = udev_monitor_receive_device(udev_monitor);
			if (device == NULL) {
				printf("no device from socket\n");
				continue;
			}
			print_device(device);
			udev_device_unref(device);
		}

		if (FD_ISSET(STDIN_FILENO, &readfds)) {
			printf("exiting loop\n");
			break;
		}
	}

	udev_monitor_unref(udev_monitor);
	return 0;
}

int main(int argc, char *argv[], char *envp[])
{
	struct udev *udev = NULL;
	static const struct option options[] = {
		{ "syspath", 1, NULL, 'p' },
		{ "subsystem", 1, NULL, 's' },
		{ "socket", 1, NULL, 'S' },
		{ "debug", 0, NULL, 'd' },
		{ "help", 0, NULL, 'h' },
		{ "version", 0, NULL, 'V' },
		{}
	};
	const char *syspath = "/devices/virtual/mem/null";
	const char *subsystem = NULL;
	const char *socket = "@/org/kernel/udev/monitor";
	char path[1024];
	const char *str;

	udev = udev_new();
	printf("context: %p\n", udev);
	if (udev == NULL) {
		printf("no context\n");
		return 1;
	}
	udev_set_log_fn(udev, log_fn);
	printf("set log: %p\n", log_fn);

	while (1) {
		int option;

		option = getopt_long(argc, argv, "+dhV", options, NULL);
		if (option == -1)
			break;

		switch (option) {
		case 'p':
			syspath = optarg;
			break;
		case 's':
			subsystem = optarg;
			break;
		case 'S':
			socket = optarg;
			break;
		case 'd':
			if (udev_get_log_priority(udev) < LOG_INFO)
				udev_set_log_priority(udev, LOG_INFO);
			break;
		case 'h':
			printf("--debug --syspath= --subsystem= --socket= --help\n");
			goto out;
		case 'V':
			printf("%s\n", VERSION);
			goto out;
		default:
			goto out;
		}
	}

	str = udev_get_sys_path(udev);
	printf("sys_path: '%s'\n", str);
	str = udev_get_dev_path(udev);
	printf("dev_path: '%s'\n", str);

	/* add sys path if needed */
	if (strncmp(syspath, udev_get_sys_path(udev), strlen(udev_get_sys_path(udev))) != 0) {
		snprintf(path, sizeof(path), "%s%s", udev_get_sys_path(udev), syspath);
		syspath = path;
	}

	test_device(udev, syspath);
	test_device_devnum(udev);
	test_device_parents(udev, syspath);
	test_enumerate(udev, subsystem);
	test_monitor(udev, socket);
out:
	udev_unref(udev);
	return 0;
}
