/*
 * Copyright (c) 2019 Exploratory Engineering
 */
#include <zephyr.h>
#include <errno.h>
#include <stdio.h>
#include <net/socket.h>
#include <at_cmd.h>
#include <at_cmd_parser.h>

K_SEM_DEFINE(registered_on_network_sem, 0, 1);

static void at_cmd_notification_handler(char *response) {
	if (memcmp(response, "+CEREG", 6) != 0) {
		return;
	}

	struct at_param_list params = {0};
	if (at_params_list_init(&params, 1) != 0) {
		printf("Error initializing AT param list.\n");
		return;
	}
	if (at_parser_params_from_str(&response[sizeof("+CEREG:")], &params) != 0) {
		printf("Error parsing response: %s\n", response);
		goto cleanup;
	}
	u16_t status = 0;
	if (at_params_short_get(&params, 0, &status) != 0) {
		printf("Error getting short param: %s\n", response);
		goto cleanup;
	}
	if (status == 1) {
		k_sem_give(&registered_on_network_sem);
	}

cleanup:
	at_params_list_free(&params);
}

static bool subscribe_network_status_notifications() {
	if (at_cmd_write("AT+CEREG=1", NULL, 0, NULL) != 0) {
		printf("Error subscribing to network status notifications.\n");
		return false;
	}
	at_cmd_set_notification_handler(at_cmd_notification_handler);
	return true;
}

static bool systemmode_lte() {
	if (at_cmd_write("AT+CFUN=4", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT%XSYSTEMMODE=1,0,0,0", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT+CFUN=1", NULL, 0, NULL) != 0) {
		printf("Error enabling system mode LTE: %d\n", errno);
		return false;
	}

	if (k_sem_take(&registered_on_network_sem, K_SECONDS(60)) != 0) {
		printf("Did not register on network within 60 seconds.\n");
		return false;
	}

	return true;
}

bool print_imei_imsi() {
	char resp[32] = {0};
	int err = at_cmd_write("AT+CGSN", resp, sizeof(resp), NULL);
	if (err != 0) {
		printf("Error getting IMEI: %d.\n", err);
		return false;
	}
	resp[15] = 0;
	printf("IMEI: %s\n", resp);

	err = at_cmd_write("AT+CIMI", resp, sizeof(resp), NULL);
	if (err != 0) {
		printf("Error getting IMSI: %d.\n", err);
		return false;
	}
	resp[15] = 0;
	printf("IMSI: %s\n", resp);

	return true;
}

bool set_apn() {
	if (at_cmd_write("AT+CFUN=1", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT+CGATT=0", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT+CGDCONT=0,\"IP\",\"mda.ee\"", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT+CGDCONT?", NULL, 0, NULL) != 0 ||
		at_cmd_write("AT+CGATT=1", NULL, 0, NULL) != 0) {
		return false;
	}

	if (k_sem_take(&registered_on_network_sem, K_SECONDS(60)) != 0) {
		printf("Did not register on network within 60 seconds.\n");
		return false;
	}

	return true;
}

bool send_message(const char *message) {
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printf("Error opening socket: %d\n", errno);
		return false;
	}

	static struct sockaddr_in remote_addr = {
		sin_family: AF_INET,
		sin_port:   htons(1234),
	};
	net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

	if (sendto(sock, message, strlen(message), 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
		printf("Error sending: %d\n", errno);
		close(sock);
		return false;
	}

	close(sock);
	return true;
}

void main() {
	if (!subscribe_network_status_notifications()) {
		goto end;
	}

	if (!systemmode_lte()) {
		printf("Failed to set system mode LTE.\n");
		goto end;
	}

	printf("Example application started.\n"); 

	if (!print_imei_imsi()) {
		printf("Failed to get IMEI/IMSI.\n");
		goto end;
	}

	if (!set_apn()) {
		printf("Failed to set APN.\n");
		goto end;
	}
	printf("Connected!\n"); 

	if (!send_message("Hello, World!")) {
		printf("Failed to send.\n");
		goto end;
	}
	printf("Message sent!\n"); 

end:
	printf("Example application complete.\n"); 
}
