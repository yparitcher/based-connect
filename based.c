#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "based.h"
#include "bluetooth.h"

#define ANY 0x00
#define CN_BASE_PACK_LEN 4

int has_noise_cancelling(unsigned int device_id) {
	switch (device_id) {
		case 0x4014:
		case 0x4020:
		case 0x400c:
			return 1;
		default:
			return 0;
	}
}

int masked_memcmp(const uint8_t *ptr1, const uint8_t *ptr2, size_t num, const uint8_t *mask) {
	while (num-- > 0) {
		uint8_t mask_byte = *(uint8_t *) mask++;
		uint8_t byte1 = *(uint8_t *) ptr1++ & mask_byte;
		uint8_t byte2 = *(uint8_t *) ptr2++ & mask_byte;

		if (byte1 != byte2) {
			return byte1 - byte2;
		}
	}
	return 0;
}

int read_check(int sock, uint8_t *recieve, size_t recieve_n, const uint8_t *ack,
		const uint8_t *mask) {
	int status = read(sock, recieve, recieve_n);
	if (status != recieve_n) {
		return status ? status : 1;
	}

//for (size_t z = 0; z<recieve_n; z++){
//	printf("0x%02x, ", *(recieve+z));
//}
//printf(":: ");
//for (size_t z = 0; z<recieve_n; z++){
//	printf("0x%02x, ", *(ack+z));
//}
//printf("\n");

	return abs(mask
			? masked_memcmp(ack, recieve, recieve_n, mask)
			: memcmp(ack, recieve, recieve_n));
}

int write_check(int sock, const void *send, size_t send_n,
		const void *ack, size_t ack_n) {
	uint8_t buffer[ack_n];

	int status = write(sock, send, send_n);
	if (status != send_n) {
		return status ? status : 1;
	}
	return read_check(sock, buffer, sizeof(buffer), ack, NULL);
}

int send_packet(int sock, const void *send, size_t send_n, uint8_t recieved[MAX_BT_PACK_LEN]) {
	int status = write(sock, send, send_n);
	if (status != send_n) {
		return status ? status : 1;
	}

	return read(sock, recieved, MAX_BT_PACK_LEN);
}

int init_connection(int sock) {
	const uint8_t send[] = { 0x00, 0x01, 0x01, 0x00 };
	const uint8_t ack[] = { 0x00, 0x01, 0x03, 0x05 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	// Throw away the initial firmware version
	uint8_t garbage[5];
	status = read(sock, garbage, sizeof(garbage));

	if (status != sizeof(garbage)) {
		return status ? status : 1;
	}

	return 0;
}

int get_device_id(int sock, unsigned int *device_id, unsigned int *index) {
	const uint8_t send[] = { 0x00, 0x03, 0x01, 0x00 };
	const uint8_t ack[] = { 0x00, 0x03, 0x03, 0x03 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	uint16_t device_id_halfword;
	status = read(sock, &device_id_halfword, sizeof(device_id_halfword));
	if (status != sizeof(device_id_halfword)) {
		return status ? status : 1;
	}

	*device_id = bswap_16(device_id_halfword);

	uint8_t index_byte;
	status = read(sock, &index_byte, 1);
	if (status != 1) {
		return status ? status : 1;
	}
	*index = index_byte;

	return 0;
}

int get_name(int sock, char name[MAX_NAME_LEN + 1]) {
	// Fifth byte is 0x01 by default and 0x00 if changed
	const uint8_t ack[] = { 0x01, 0x02, 0x03, ANY, ANY };
	const uint8_t mask[] = { 0xff, 0xff, 0xff, 0x00, 0x00 };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	size_t length = buffer[3] - 1;
	status = read(sock, name, length);
	if (status != length) {
		return status ? status : 1;
	}
	name[length] = '\0';

	return 0;
}

int set_name(int sock, const char *name) {
	uint8_t send[CN_BASE_PACK_LEN + MAX_NAME_LEN] = { 0x01, 0x02, 0x02, ANY };
	size_t length = strlen(name);

	send[3] = length;
	strncpy((char *) &send[CN_BASE_PACK_LEN], name, MAX_NAME_LEN);

	size_t send_size = CN_BASE_PACK_LEN + length;
	int status = write(sock, send, send_size);
	if (status != send_size) {
		return status ? status : 1;
	}

	char got_name[MAX_NAME_LEN + 1];
	status = get_name(sock, got_name);
	if (status) {
		return status;
	}

	return abs(strcmp(name, got_name));
}

int get_prompt_language(int sock, enum PromptLanguage *language) {
	// TODO: ensure that this value is correct
	// TODO: figure out what bytes 6 and 7 are for
	const uint8_t ack[] = { 0x01, 0x03, 0x03, 0x05, ANY, 0x00, ANY, ANY, 0xde };
	const uint8_t mask[] = { 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	*language = buffer[4];
	return 0;
}

int set_prompt_language(int sock, enum PromptLanguage language) {
	uint8_t send[] = { 0x01, 0x03, 0x02, 0x01, ANY };
	send[4] = language;

	int status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	enum PromptLanguage got_language;
	status = get_prompt_language(sock, &got_language);
	if (status) {
		return status;
	}

	return abs(language - got_language);
}

int set_voice_prompts(int sock, int on) {
	char name[MAX_NAME_LEN + 1];
	enum PromptLanguage pl;
	enum AutoOff ao;
	enum NoiseCancelling nc;
	enum ActionButton ab;
	enum SelfVoice sv;

	int status = get_device_status(sock, name, &pl, &ao, &nc, &ab, &sv);
	if (status) {
		return status;
	}

	if (on) {
		pl |= VP_MASK;
	} else {
		pl &= ~VP_MASK;
	}

	return set_prompt_language(sock, pl);
}

int get_auto_off(int sock, enum AutoOff *minutes) {
	const uint8_t ack[] = { 0x01, 0x04, 0x03, 0x01, ANY };
	const uint8_t mask[] = { 0xff, 0xff, 0xff, 0xff, 0x00 };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	*minutes = buffer[4];
	return 0;
}

int set_auto_off(int sock, enum AutoOff minutes) {
	uint8_t send[] = { 0x01, 0x04, 0x02, 0x01, ANY };
	send[4] = minutes;

	int status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	enum AutoOff got_minutes;
	status = get_auto_off(sock, &got_minutes);
	if (status) {
		return status;
	}

	return abs(minutes - got_minutes);
}

int get_noise_cancelling(int sock, enum NoiseCancelling *level) {
	const uint8_t ack[] = { 0x01, 0x06, 0x03, 0x02, ANY, 0x0b };
	const uint8_t mask[] = { 0xff, 0xff, 0xff, 0xff, 0x00, 0xff };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	*level = buffer[4];
	return 0;
}

int set_noise_cancelling(int sock, enum NoiseCancelling level) {
	uint8_t send[] = { 0x01, 0x06, 0x02, 0x01, ANY };
	send[4] = level;

	int status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	enum NoiseCancelling got_level;
	status = get_noise_cancelling(sock, &got_level);
	if (status) {
		return status;
	}

	return abs(level - got_level);
}

int get_action_button(int sock, enum ActionButton *actionbutton) {
	uint8_t ack[] = { 0x01, 0x09, 0x03, 0x04, 0x10, 0x04,  ANY, 0x07 };
	uint8_t mask[] =  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	*actionbutton = buffer[6];
	return 0;
}

int set_action_button(int sock, enum ActionButton actionbutton) {
	uint8_t send[] = { 0x01, 0x09, 0x02, 0x03, 0x10, 0x04, ANY };
	send[6] = actionbutton;

	int status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	enum ActionButton got_actionbutton;
	status = get_action_button(sock, &got_actionbutton);
	if (status) {
		return status;
	}

	return abs(actionbutton - got_actionbutton);
}

#include <errno.h>
void print_payload(int sock)
{
	size_t buffer_n = sizeof(uint8_t);
	int status = buffer_n;
	printf("Extra device info bytes:\n");
	for (; status == buffer_n;)
	{
		uint8_t buffer[sizeof(uint8_t)] = {0};
		status = read(sock, buffer, buffer_n);
		printf("0x%02X, ", buffer[0]);
	}
	if (status == -1){
		printf("Error: %s", strerror(errno));
	}
	printf("\nread exit code: %d\n", status);
}

int get_device_status(int sock, char name[MAX_NAME_LEN + 1], enum PromptLanguage *language,
		enum AutoOff *minutes, enum NoiseCancelling *level, enum ActionButton *actionbutton, enum SelfVoice *selfvoice) {
	unsigned int device_id;
	unsigned int index;
	int status = get_device_id(sock, &device_id, &index);
	if (status) {
		return status;
	}

	const uint8_t send[] = { 0x01, 0x01, 0x05, 0x00 };
	status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	const uint8_t ack1[] = { 0x01, 0x01, 0x07, 0x00 };
	uint8_t buffer1[sizeof(ack1)] = {0};

	status = read_check(sock, buffer1, sizeof(buffer1), ack1, NULL);
	if (status) {
		return status;
	}

	status = get_name(sock, name);
	if (status) {
		return status;
	}

	status = get_prompt_language(sock, language);
	if (status) {
		return status;
	}

	status = get_auto_off(sock, minutes);
	if (status) {
		return status;
	}

	if (has_noise_cancelling(device_id)) {
		status = get_noise_cancelling(sock, level);
		if (status) {
			return status;
		}
	} else {
		*level = NC_DNE;
	}

	status = get_action_button(sock, actionbutton);
	if (status) {
		return status;
	}

	status = get_self_voice(sock, selfvoice);
	if (status) {
		return status;
	}

	const uint8_t ack2[] = { 0x01, 0x01, 0x06, 0x00 };
	uint8_t buffer2[sizeof(ack2)] = {0};
	return read_check(sock, buffer2, sizeof(buffer2), ack2, NULL);
}

int set_pairing(int sock, enum Pairing pairing) {
	uint8_t send[] = { 0x04, 0x08, 0x05, 0x01, ANY };
	uint8_t ack[] = { 0x04, 0x08, 0x06, 0x01, ANY };
	send[4] = pairing;
	ack[4] = pairing;
	return write_check(sock, send, sizeof(send), ack, sizeof(ack));
}

int get_self_voice(int sock, enum SelfVoice *selfvoice) {
	uint8_t ack[] =  { 0x01, 0x0b, 0x03, 0x03, 0x01,  ANY, 0x0f};
	uint8_t mask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff };
	uint8_t buffer[sizeof(ack)] = {0};

	int status = read_check(sock, buffer, sizeof(buffer), ack, mask);
	if (status) {
		return status;
	}

	*selfvoice = buffer[5];
	return 0;
}

int set_self_voice(int sock, enum SelfVoice selfvoice) {
	uint8_t send[] = { 0x01, 0x0b, 0x02, 0x02, 0x01, ANY, 0x38 };

	send[5] = selfvoice;
	int status = write(sock, send, sizeof(send));
	if (status != sizeof(send)) {
		return status ? status : 1;
	}

	enum SelfVoice got_selfvoice;
	status = get_self_voice(sock, &got_selfvoice);
	if (status) {
		return status;
	}

	return abs(selfvoice - got_selfvoice);
}

int get_firmware_version(int sock, char version[VER_STR_LEN]) {
	const uint8_t send[] = { 0x00, 0x05, 0x01, 0x00 };
	const uint8_t ack[] = { 0x00, 0x05, 0x03, 0x05 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	status = read(sock, version, VER_STR_LEN - 1);
	if (status != VER_STR_LEN - 1) {
		return status ? status : 1;
	}

	version[VER_STR_LEN - 1] = '\0';
	return 0;
}

int get_serial_number(int sock, char serial[0x100]) {
	const uint8_t send[] = { 0x00, 0x07, 0x01, 0x00 };
	const uint8_t ack[] = { 0x00, 0x07, 0x03 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	uint8_t length;
	status = read(sock, &length, 1);
	if (status != 1) {
		return status ? status : 1;
	}

	status = read(sock, serial, length);
	if (status != length) {
		return status ? status : 1;
	}
	serial[length] = '\0';

	return 0;
}

int get_battery_level(int sock, unsigned int *level) {
	const uint8_t send[] = { 0x02, 0x02, 0x01, 0x00 };
	const uint8_t ack[] = { 0x02, 0x02, 0x03, 0x01 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	uint8_t level_byte;
	status = read(sock, &level_byte, 1);
	*level = level_byte;
	return 0;
}

int get_device_info(int sock, bdaddr_t address, struct Device *device) {
	uint8_t send[10] = { 0x04, 0x05, 0x01, BT_ADDR_LEN };
	const uint8_t ack[] = { 0x04, 0x05, 0x03 };

	memcpy(&send[4], &address.b, BT_ADDR_LEN);

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	uint8_t length;
	status = read(sock, &length, 1);
	if (status != 1) {
		return status ? status : 1;
	}

	status = read(sock, &device->address.b, BT_ADDR_LEN);
	if (status != BT_ADDR_LEN) {
		return status ? status : 1;
	}
	length -= BT_ADDR_LEN;

	status = memcmp(&address.b, &device->address.b, BT_ADDR_LEN);
	if (status) {
		return abs(status);
	}

	uint8_t status_byte;
	status = read(sock, &status_byte, 1);
	if (status != 1) {
		return status ? status : 1;
	}
	length -= 1;

	device->status = status_byte;

	// TODO: figure out what the first byte of garbage is for
	uint8_t garbage[2];
	status = read(sock, &garbage, sizeof(garbage));
	if (status != sizeof(garbage)) {
		return status ? status : 1;
	}
	length -= sizeof(garbage);

	status = read(sock, device->name, length);
	if (status != length) {
		return status ? status : 1;
	}
	device->name[length] = '\0';

	return 0;
}

int get_paired_devices(int sock, bdaddr_t addresses[MAX_NUM_DEVICES], size_t *num_devices,
		enum DevicesConnected *connected) {
	const uint8_t send[] = { 0x04, 0x04, 0x01, 0x00 };
	const uint8_t ack[] = { 0x04, 0x04, 0x03 };

	int status = write_check(sock, send, sizeof(send), ack, sizeof(ack));
	if (status) {
		return status;
	}

	uint8_t num_devices_byte;
	status = read(sock, &num_devices_byte, 1);
	if (status != 1) {
		return status ? status : 1;
	}

	// num_devices_byte = (num_devices_byte - 1) / BT_ADDR_LEN;
	// equivalent statements but more efficient
	num_devices_byte /= BT_ADDR_LEN;

	*num_devices = num_devices_byte;

	uint8_t num_connected_byte;
	status = read(sock, &num_connected_byte, 1);
	if (status != 1) {
		return status ? status : 1;
	}
	*connected = num_connected_byte;

	size_t i;
	for (i = 0; i < num_devices_byte; ++i) {
		status = read(sock, &addresses[i].b, BT_ADDR_LEN);
		if (status != BT_ADDR_LEN) {
			return status ? status : 1;
		}
	}

	return 0;
}

int connect_device(int sock, bdaddr_t address) {
	uint8_t send[11] = { 0x04, 0x01, 0x05, BT_ADDR_LEN + 1, 0x00 };
	uint8_t ack[10] = { 0x04, 0x01, 0x07, BT_ADDR_LEN };
	memcpy(&send[5], &address.b, BT_ADDR_LEN);
	memcpy(&ack[4], &address.b, BT_ADDR_LEN);
	return write_check(sock, send, sizeof(send), ack, sizeof(ack));
}

int disconnect_device(int sock, bdaddr_t address) {
	uint8_t send[10] = { 0x04, 0x02, 0x05, BT_ADDR_LEN };
	uint8_t ack[10] = { 0x04, 0x02, 0x07, BT_ADDR_LEN };
	memcpy(&send[4], &address.b, BT_ADDR_LEN);
	memcpy(&ack[4], &address.b, BT_ADDR_LEN);
	return write_check(sock, send, sizeof(send), ack, sizeof(ack));
}

int remove_device(int sock, bdaddr_t address) {
	uint8_t send[10] = { 0x04, 0x03, 0x05, BT_ADDR_LEN };
	uint8_t ack[10] = { 0x04, 0x03, 0x06, BT_ADDR_LEN };
	memcpy(&send[4], &address.b, BT_ADDR_LEN);
	memcpy(&ack[4], &address.b, BT_ADDR_LEN);
	return write_check(sock, send, sizeof(send), ack, sizeof(ack));
}
