/***************************************************************************
 *   Copyright (C) 2009 by Zachary T Welch <zw@superlucidity.net>          *
 *                                                                         *
 *   Copyright (C) 2011 by Mauro Gamba <maurillo71@gmail.com>              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "log.h"
#include "libusb1_common.h"

static struct libusb_context *jtag_libusb_context; /**< Libusb context **/
static libusb_device **devs; /**< The usb device list **/

static bool jtag_libusb_match_ids(struct libusb_device_descriptor *dev_desc,
		const uint16_t vids[], const uint16_t pids[])
{
	for (unsigned i = 0; vids[i]; i++) {
		if (dev_desc->idVendor == vids[i] &&
			dev_desc->idProduct == pids[i]) {
			return true;
		}
	}
	return false;
}

/*
 * Compute ST-Link serial number from the device descriptor
 * "old" ST-Link DFU returns 12 8-bits values, separated by zeros (wrong unicode encoding)
 */

static int stlink_compute_serial(unsigned char *desc_serial, char *serial)
{
	int i, len = desc_serial[0];

	if (len == 26) /* work-around for old ST-Links*/
		for (i = 0; i < 12; i++)
			sprintf((char *) &(serial[i*2]), "%02hX", (unsigned short) desc_serial[2 * (i+1)]);
	else if (len == 50)
		for (i = 0; i < 24; i++)
			serial[i] = desc_serial[2 * (i + 1)];
	else {
		LOG_ERROR("unexpected serial length (%d) in descriptor", len);
		return ERROR_FAIL;
	}

	serial[24] = '\0';

	return ERROR_OK;
}

/* Returns true if the serial in the descriptor matches the given serial */
static bool jtag_libusb_match_serial(libusb_device_handle *device,
		struct libusb_device_descriptor *dev_desc, const char *user_serial)
{
	int retval;
	bool matched;
	char device_serial[256+1]; /* Max size of string descriptor */

	if (dev_desc->iSerialNumber == 0)
		return false;

	if (dev_desc->idVendor == 0x0483) { /* work around for ST-Link adapter */
		char device_desc_serial[256+1];
		retval = libusb_get_string_descriptor(device, dev_desc->iSerialNumber, 0x409,
					(unsigned char *)device_desc_serial, 64);
		if (retval < 0) {
			LOG_ERROR("libusb_get_string_descriptor() failed with %d", retval);
			return false;
		}
		if (stlink_compute_serial((unsigned char *)device_desc_serial, device_serial) != ERROR_OK)
			return false;
	} else {
		retval = libusb_get_string_descriptor_ascii(device, dev_desc->iSerialNumber,
					(unsigned char *)device_serial, sizeof(device_serial)-1);
		if (retval < 0) {
			LOG_ERROR("libusb_get_string_descriptor_ascii() failed with %d", retval);
			return false;
		}
	}

	/* Null terminate descriptor string in case it needs to be logged. */
	device_serial[sizeof(device_serial)-1] = '\0';

	matched = strncmp(user_serial, device_serial, sizeof(device_serial)) == 0;
	if (!matched)
		LOG_DEBUG("Device serial number '%s' doesn't match requested serial '%s'",
			device_serial, user_serial);

	return matched;
}

int jtag_libusb_open(const uint16_t vids[], const uint16_t pids[],
		const char *serial,
		struct jtag_libusb_device_handle **out)
{
	int cnt, idx, errCode;
	int retval = ERROR_FAIL;
	bool serial_mismatch = false;
	struct jtag_libusb_device_handle *libusb_handle = NULL;

	if (libusb_init(&jtag_libusb_context) < 0)
		return ERROR_FAIL;

	cnt = libusb_get_device_list(jtag_libusb_context, &devs);

	for (idx = 0; idx < cnt; idx++) {
		struct libusb_device_descriptor dev_desc;

		if (libusb_get_device_descriptor(devs[idx], &dev_desc) != 0)
			continue;

		if (!jtag_libusb_match_ids(&dev_desc, vids, pids))
			continue;

		errCode = libusb_open(devs[idx], &libusb_handle);

		if (errCode) {
			LOG_ERROR("libusb_open() failed with %s",
				  libusb_error_name(errCode));
			continue;
		}

		/* Device must be open to use libusb_get_string_descriptor_ascii. */
		if (serial != NULL && !jtag_libusb_match_serial(libusb_handle, &dev_desc, serial)) {
			serial_mismatch = true;
			libusb_close(libusb_handle);
			continue;
		}

		/* Success. */
		*out = libusb_handle;
		retval = ERROR_OK;
		serial_mismatch = false;
		break;
	}
	if (cnt >= 0)
		libusb_free_device_list(devs, 1);

	if (serial_mismatch)
		LOG_DEBUG("No device matches the serial string");

	return retval;
}

void jtag_libusb_close(jtag_libusb_device_handle *dev)
{
	/* Close device */
	libusb_close(dev);

	libusb_exit(jtag_libusb_context);
}

int jtag_libusb_control_transfer(jtag_libusb_device_handle *dev, uint8_t requestType,
		uint8_t request, uint16_t wValue, uint16_t wIndex, char *bytes,
		uint16_t size, unsigned int timeout)
{
	int transferred = 0;

	transferred = libusb_control_transfer(dev, requestType, request, wValue, wIndex,
				(unsigned char *)bytes, size, timeout);

	if (transferred < 0)
		transferred = 0;

	return transferred;
}

int jtag_libusb_bulk_write(jtag_libusb_device_handle *dev, int ep, char *bytes,
		int size, int timeout)
{
	int transferred = 0;

	libusb_bulk_transfer(dev, ep, (unsigned char *)bytes, size,
			     &transferred, timeout);
	return transferred;
}

int jtag_libusb_bulk_read(jtag_libusb_device_handle *dev, int ep, char *bytes,
		int size, int timeout)
{
	int transferred = 0;

	libusb_bulk_transfer(dev, ep, (unsigned char *)bytes, size,
			     &transferred, timeout);
	return transferred;
}

int jtag_libusb_set_configuration(jtag_libusb_device_handle *devh,
		int configuration)
{
	struct jtag_libusb_device *udev = jtag_libusb_get_device(devh);
	int retCode = -99;

	struct libusb_config_descriptor *config = NULL;
	int current_config = -1;

	retCode = libusb_get_configuration(devh, &current_config);
	if (retCode != 0)
		return retCode;

	retCode = libusb_get_config_descriptor(udev, configuration, &config);
	if (retCode != 0 || config == NULL)
		return retCode;

	/* Only change the configuration if it is not already set to the
	   same one. Otherwise this issues a lightweight reset and hangs
	   LPC-Link2 with JLink firmware. */
	if (current_config != config->bConfigurationValue)
		retCode = libusb_set_configuration(devh, config->bConfigurationValue);

	libusb_free_config_descriptor(config);

	return retCode;
}

int jtag_libusb_choose_interface(struct jtag_libusb_device_handle *devh,
		unsigned int *usb_read_ep,
		unsigned int *usb_write_ep,
		int bclass, int subclass, int protocol, int trans_type)
{
	struct jtag_libusb_device *udev = jtag_libusb_get_device(devh);
	const struct libusb_interface *inter;
	const struct libusb_interface_descriptor *interdesc;
	const struct libusb_endpoint_descriptor *epdesc;
	struct libusb_config_descriptor *config;

	*usb_read_ep = *usb_write_ep = 0;

	libusb_get_config_descriptor(udev, 0, &config);
	for (int i = 0; i < (int)config->bNumInterfaces; i++) {
		inter = &config->interface[i];

		interdesc = &inter->altsetting[0];
		for (int k = 0;
		     k < (int)interdesc->bNumEndpoints; k++) {
			if ((bclass > 0 && interdesc->bInterfaceClass != bclass) ||
			    (subclass > 0 && interdesc->bInterfaceSubClass != subclass) ||
			    (protocol > 0 && interdesc->bInterfaceProtocol != protocol))
				continue;

			epdesc = &interdesc->endpoint[k];
			if (trans_type > 0 && (epdesc->bmAttributes & 0x3) != trans_type)
				continue;

			uint8_t epnum = epdesc->bEndpointAddress;
			bool is_input = epnum & 0x80;
			LOG_DEBUG("usb ep %s %02x",
				  is_input ? "in" : "out", epnum);

			if (is_input)
				*usb_read_ep = epnum;
			else
				*usb_write_ep = epnum;

			if (*usb_read_ep && *usb_write_ep) {
				LOG_DEBUG("Claiming interface %d", (int)interdesc->bInterfaceNumber);
				libusb_claim_interface(devh, (int)interdesc->bInterfaceNumber);
				libusb_free_config_descriptor(config);
				return ERROR_OK;
			}
		}
	}
	libusb_free_config_descriptor(config);

	return ERROR_FAIL;
}

int jtag_libusb_get_pid(struct jtag_libusb_device *dev, uint16_t *pid)
{
	struct libusb_device_descriptor dev_desc;

	if (libusb_get_device_descriptor(dev, &dev_desc) == 0) {
		*pid = dev_desc.idProduct;

		return ERROR_OK;
	}

	return ERROR_FAIL;
}
