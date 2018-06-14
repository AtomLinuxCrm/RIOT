/*
 * Copyright (C) 2018 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-modbus.c
 * @brief       umdk-modbus module implementation
 * @author      Mikhail Perkov
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_MODBUS_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "modbus"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "periph/gpio.h"
#include "periph/uart.h"

#include "board.h"

#include "unwds-common.h"
#include "include/umdk-modbus.h"

#include "thread.h"
#include "xtimer.h"
#include "rtctimers-millis.h"

#include "checksum/ucrc16.h"

#define ENABLE_DEBUG 0
#include "debug.h"

static uwnds_cb_t *callback;
static uint8_t *rxbuf;
static uint8_t *txbuf;

static volatile uint8_t num_bytes_rx;

static kernel_pid_t radio_pid;

static msg_t msg_rx 			= { .type = UMDK_MODBUS_MSG_RADIO, };
static msg_t msg_rx_full 		= { .type = UMDK_MODBUS_MSG_OVERFLOW, };
static msg_t msg_no_response 	= { .type = UMDK_MODBUS_MSG_NO_RESPONSE, };

static xtimer_t rx_timer;
static umdk_modbus_config_t umdk_modbus_config = { UMDK_MODBUS_DEV, UMDK_MODBUS_BAUDRATE_DEF, UART_DATABITS_8, \
													UART_PARITY_NOPARITY, UART_STOPBITS_10 };

static umdk_modbus_config_pack_t current_pack = { UMDK_MODBUS_NOT_RECIEVED, UMDK_MODBUS_RX_NOT_ALLOW, \
												UMDK_MODBUS_RADIO, UMDK_MODBUS_RESPONSE, true, 0, 0, };

static uint32_t time_wait = UMDK_MODBUS_TIMEWAIT_DEF_USEC;
static volatile uint8_t id_device = 0;

static void _send_pack(uint8_t length);
static bool _check_pack(uint8_t length);
static void _check_respose(uint32_t min_delay, uint32_t time_response);
static void _transmit_pack(void);

static void _transmit_pack(void)
{		/* Allow receiving data*/
	current_pack.rx_allow = UMDK_MODBUS_RX_ALLOW;
		/* Data is not received */
	current_pack.flag_rx = UMDK_MODBUS_NOT_RECIEVED;
		/* There is response */
	current_pack.response = UMDK_MODBUS_RESPONSE;
		/* RX pack is valid */
	current_pack.is_true = true;
		/* Allow send data to radio */
	current_pack.radio = UMDK_MODBUS_RADIO;
	
	/* Clear counter of the rx data */
	num_bytes_rx = 0;
	
	 /* Calculate crc */
	uint16_t crc_tx = ucrc16_calc_le(txbuf, current_pack.length_tx, MODBUS_CRC16_POLY, MODBUS_CRC16_INIT);
    /* Adding crc into sending buffer */
    memcpy(txbuf + current_pack.length_tx, (uint8_t *)(&crc_tx),  sizeof(crc_tx));

    current_pack.length_tx += sizeof(crc_tx);
	
#if ENABLE_DEBUG
    DEBUG("\n");
    DEBUG("PACK into device:  ");
    for(uint8_t i = 0; i < current_pack.length_tx; i++) {
        DEBUG(" %02X ", txbuf[i]);
    }
    DEBUG("\n");
#endif

    /* Send pack */
    _send_pack(current_pack.length_tx);
	return;
}

static void _send_pack(uint8_t length)
{
	/* Send data */
	gpio_set(UMDK_MODBUS_RE_PIN);
	gpio_set(UMDK_MODBUS_DE_PIN);
	
	/*ModBus timeout*/
	xtimer_usleep(time_wait);

    uart_write(UART_DEV(umdk_modbus_config.uart_dev), txbuf, length);
	
	/*ModBus timeout*/
	// xtimer_usleep(time_wait);
	
	/* Transmitter is off (End of send data)*/
	gpio_clear(UMDK_MODBUS_RE_PIN);
	gpio_clear(UMDK_MODBUS_DE_PIN);
}

static bool _check_pack(uint8_t length)
{
		/* Minimum length - 1 byte Id, 1 byte cmd, 2 bytes crc */
	if(length < 4) {
		puts("[umdk-" _UMDK_NAME_ "] Error -> No received data");
		return false;		
	}

	/* Check for empty data */
	uint8_t check_zero_buf = 2;	
	for(uint8_t i = 0; i < length; i++) {
		if(rxbuf[i] != 0) { 
			check_zero_buf = 0;
			break;
		}
		else {
			check_zero_buf = 1;
		}
	}
		 /* If the received data is empty */
	if(check_zero_buf == 1) {
		num_bytes_rx = 0;
		puts("[umdk-" _UMDK_NAME_ "] Error -> Empty received data");
		return false;
	}	
	
		/* Check ID device */
	if(rxbuf[0] > MODBUS_MAX_ID) {
		puts("[umdk-" _UMDK_NAME_ "] Error -> Invalid received ID");
		return false;
	}
	
		/* Recevied CRC */
    uint16_t crc_rx = (rxbuf[length - 2] << 0) +  (rxbuf[length - 1] << 8);
		/* Calculate rx CRC */
	uint16_t crc = ucrc16_calc_le(rxbuf, length - 2, MODBUS_CRC16_POLY, MODBUS_CRC16_INIT);

	DEBUG("CRC / RX_CRC: %04X / %04X \n", crc, crc_rx);
	
		/* Check CRCs */
    if(crc != crc_rx) {
        printf("[umdk-" _UMDK_NAME_ "] Error -> Wrong received CRC");
        return false;
    }

    return true;
}

static void _check_respose(uint32_t min_delay, uint32_t time_response)
{
	uint32_t num_detect = 0;
	/* Wait for the minimum time to receive data */
	rtctimers_millis_sleep(min_delay);

	while((num_detect * min_delay) <= time_response) {
		/* Check flag of recieved data */
		if(current_pack.flag_rx == UMDK_MODBUS_RECIEVED) {
				/* There is response */
			current_pack.response = UMDK_MODBUS_RESPONSE;				
			rtctimers_millis_sleep(min_delay);
				/* Wait the end of the current command */
			if(current_pack.rx_allow == UMDK_MODBUS_RX_ALLOW) {
				rtctimers_millis_sleep(min_delay);
			}
			return;
		}
		else {
			/* If the data is not yet received, wait */
			rtctimers_millis_sleep(min_delay);
		}		
		num_detect++;
	}

	/* If the data is not received */
	/* No response was obtained */
	current_pack.response = UMDK_MODBUS_NO_RESPONSE;
	/* Do not allow receiving data */
	current_pack.rx_allow = UMDK_MODBUS_RX_NOT_ALLOW;
	/* Data is not received */
	current_pack.flag_rx = UMDK_MODBUS_NOT_RECIEVED;
		/* Sending to radio msg "No response" */
	msg_try_send(&msg_no_response, radio_pid);
	
	return;	
}

void *radio_send(void *arg) {
    (void)arg;
    
    msg_t msg;
    msg_t msg_queue[128];
    msg_init_queue(msg_queue, 128);

    while (1) {
        msg_receive(&msg);

        module_data_t data;
        data.as_ack = true;
        data.data[0] = _UMDK_MID_;
        data.length = 1;
		
		modbus_msg_t  msg_type = (modbus_msg_t)msg.type;
		
		switch(msg_type) {
			case UMDK_MODBUS_MSG_NO_RESPONSE: {	/* "No response" */
				puts("[umdk-" _UMDK_NAME_ "] Error -> No response");
				/* Add to radiodata ID current device */	
				data.data[1] = id_device;			
				data.data[2] = UMDK_MODBUS_NO_RESPONSE_REPLY;
				data.length += 2;				
				/* Clear counter of the rx data */
				num_bytes_rx = 0;
				callback(&data);
				break;
			}
			
			case UMDK_MODBUS_MSG_OVERFLOW: {	/* Rx buffer overflow */
				puts("[umdk-" _UMDK_NAME_ "] Error -> Buffer overflow");
				/* Do not allow receiving data*/
				current_pack.rx_allow = UMDK_MODBUS_RX_NOT_ALLOW;
				/* Data is received */
				current_pack.flag_rx = UMDK_MODBUS_RECIEVED;
				/* Not Allow send data to radio */
				current_pack.radio = UMDK_MODBUS_NO_RADIO;
				/* Add to radiodata ID current device */	
				data.data[1] = id_device;
				data.data[2] = UMDK_MODBUS_OVERFLOW_REPLY;
				data.length += 2;
				callback(&data);
				break;
			}
			
			case UMDK_MODBUS_MSG_RADIO: {	/* Recevied data */
#if ENABLE_DEBUG
    DEBUG("\n");
    DEBUG("Data from DEVICE:  ");
    for(uint8_t i = 0; i < cmd->length; i++) {
        DEBUG(" %02X ", cmd->data[i]);
    }
    DEBUG("\n");
#endif
				/* Data is received */
				current_pack.flag_rx = UMDK_MODBUS_RECIEVED;
				current_pack.length_rx = num_bytes_rx;
				num_bytes_rx = 0;
					/*if allow send radiodata*/
				if(current_pack.radio == UMDK_MODBUS_RADIO) {
						/* Check RX pack */
					current_pack.is_true = _check_pack(current_pack.length_rx);
						/* valid pack */
					if(current_pack.is_true) {
						memcpy(data.data + 1, rxbuf, current_pack.length_rx - 2);
						data.length += current_pack.length_rx - 2;
					}
					else {	/* invalid pack */
						/* Add to data in radio ID current device */	
						data.data[1] = id_device;
						data.length++;
						data.data[2] = UMDK_MODBUS_ERROR_REPLY;
						data.length++;
					}				
					/* Do not Allow send data to radio */
					current_pack.radio = UMDK_MODBUS_NO_RADIO;
			
					callback(&data);
					
				}
				/* Do not allow receiving data*/
				current_pack.rx_allow = UMDK_MODBUS_RX_NOT_ALLOW;
				break;
			}
			
			default:
				break;
		
		}
    }

    return NULL;
}

void umdk_modbus_handler(void *arg, uint8_t data)
{
    (void)arg;
	/* if allow receiving data*/
    if(current_pack.rx_allow == UMDK_MODBUS_RX_ALLOW)  {
		 /* Buffer overflow */
		if (num_bytes_rx >= UMDK_MODBUS_BUFF_SIZE) {
			num_bytes_rx = 0;
			/* Do not allow receiving data*/
			current_pack.rx_allow = UMDK_MODBUS_RX_NOT_ALLOW;
				/* Send msg "Rx buff overflow" */
			msg_try_send(&msg_rx_full, radio_pid);
			return;
		}

		rxbuf[num_bytes_rx++] = data;
		/* Schedule sending after timeout */
		xtimer_set_msg(&rx_timer, time_wait, &msg_rx, radio_pid);
	}
	else {
		return;
	}
}

static void reset_config(void) {
	umdk_modbus_config.baudrate = UMDK_MODBUS_BAUDRATE_DEF;
    umdk_modbus_config.databits = UART_DATABITS_8;
    umdk_modbus_config.parity = UART_PARITY_NOPARITY;
    umdk_modbus_config.stopbits = UART_STOPBITS_20;
	umdk_modbus_config.uart_dev = UMDK_UART_DEV;
}

static inline void save_config(void) {
	unwds_write_nvram_config(_UMDK_MID_, (uint8_t *) &umdk_modbus_config, sizeof(umdk_modbus_config));
}

static void init_config(void) {
	reset_config();
	
	if (!unwds_read_nvram_config(_UMDK_MID_, (uint8_t *) &umdk_modbus_config, sizeof(umdk_modbus_config))) {
		reset_config();
        return;
    }
	
    if (umdk_modbus_config.stopbits > UART_STOPBITS_20) {
		reset_config();
		return;
    }
	
	if (umdk_modbus_config.parity > UART_PARITY_EVEN) {
		reset_config();
		return;
    }
	
	if (umdk_modbus_config.databits > UART_DATABITS_9) {
		reset_config();
		return;
    }

	if (umdk_modbus_config.uart_dev >= UART_NUMOF) {
		reset_config();
		return;
	}
	
	if(umdk_modbus_config.baudrate > UMDK_MODBUS_BAUDRATE_MAX) {
		reset_config();
		return;
	}
	
	if(umdk_modbus_config.baudrate < UMDK_MODBUS_BAUDRATE_MIN) {
		reset_config();
		return;
	}
}


void umdk_modbus_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;
    callback = event_callback;
	
    init_config();

	/* Initialize DE/RE pins */
    gpio_init(UMDK_MODBUS_DE_PIN, GPIO_OUT);
    gpio_init(UMDK_MODBUS_RE_PIN, GPIO_OUT);
    
    gpio_clear(UMDK_MODBUS_DE_PIN);
    gpio_clear(UMDK_MODBUS_RE_PIN);	
	
    /* Initialize the ModBus params*/
    uart_params_t params;
    params.baudrate = umdk_modbus_config.baudrate;
    params.databits = umdk_modbus_config.databits;
    params.parity = umdk_modbus_config.parity;
    params.stopbits = umdk_modbus_config.stopbits;
	
	uint8_t device = umdk_modbus_config.uart_dev;
	char parity = umdk_modbus_config.parity;
    uint8_t stopbits = umdk_modbus_config.stopbits;;
	uint32_t baudrate = umdk_modbus_config.baudrate;
	uint8_t databits = umdk_modbus_config.databits + 8;
	
	if(umdk_modbus_config.stopbits == UART_STOPBITS_10) {
		stopbits = 1;
	}
	else if(umdk_modbus_config.stopbits == UART_STOPBITS_20) {
		stopbits = 2;
	}
	
    if(umdk_modbus_config.parity == UART_PARITY_NOPARITY) {
		parity = 'N';
	}
	else if(umdk_modbus_config.parity == UART_PARITY_ODD) {
		parity = 'O';
	}
	else if(umdk_modbus_config.parity == UART_PARITY_EVEN) {
		parity = 'E';
	}
	 
	 /* Initialize UART */
    if (uart_init_ext(UART_DEV(umdk_modbus_config.uart_dev), &params, umdk_modbus_handler, NULL)) {
		return;
    }
	else {
		printf("[umdk-" _UMDK_NAME_ "] Device: %02d Mode: %lu-%u%c%u\n", device, baudrate, databits, parity, stopbits);
	}
    
	
	/* Create handler thread */
	char *stack = (char *) allocate_stack(UMDK_MODBUS_STACK_SIZE);
	if (!stack) {
		puts("[umdk-" _UMDK_NAME_ "] unable to allocate memory. Is too many modules enabled?");
		return;
    }

    txbuf = (uint8_t *)stack;
    stack += UMDK_MODBUS_BUFF_SIZE;
    
    rxbuf = (uint8_t *)stack;
    stack += UMDK_MODBUS_BUFF_SIZE;
	
	memset(rxbuf, 0, UMDK_MODBUS_BUFF_SIZE);
    memset(txbuf, 0, UMDK_MODBUS_BUFF_SIZE);
		
	/* Create handler thread */
	radio_pid = thread_create(stack, UMDK_MODBUS_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, radio_send, NULL, "modbus thread");
	
	if(umdk_modbus_config.baudrate > UMDK_MODBUS_BAUDRATE_DEF) {
		time_wait = UMDK_MODBUS_TIMEWAIT_DEF_USEC;
	}
	else {
		time_wait = (uint32_t)((MODBUS_FRAME_BIT * MODBUS_WAIT_FRAME * UMDK_MODBUS_USEC_IN_SEC) / baudrate);
	}
}

static inline void reply_code(module_data_t *reply, umdk_modbus_reply_t code) 
{
	reply->as_ack = true;
	reply->length = 2;
	reply->data[0] = _UMDK_MID_;
		/* Add reply-code */
	reply->data[1] = code;
}

bool umdk_modbus_cmd(module_data_t *cmd, module_data_t *reply)
{	
	if (cmd->length < 1) {
		reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
        return true;
    }	
	
	if((cmd->data[0] == UMDK_MODBUS_INVALID_CMD_REPLY) && (cmd->length == 1)) {
		puts("[umdk-" _UMDK_NAME_ "] Invalid command");
		reply_code(reply, UMDK_MODBUS_INVALID_CMD_REPLY);
		return true;		
	}
	if((cmd->data[0] == UMDK_MODBUS_INVALID_FORMAT) && (cmd->length == 1)) {
		puts("[umdk-" _UMDK_NAME_ "] Invalid format");
		reply_code(reply, UMDK_MODBUS_INVALID_FORMAT);
		return true;		
	}	
		
#if ENABLE_DEBUG
    DEBUG("\n");
    DEBUG("Data from BASE:  ");
    for(uint8_t i = 0; i < cmd->length; i++) {
        DEBUG(" %02X ", cmd->data[i]);
    }
    DEBUG("\n");
#endif

	uint8_t command = cmd->data[0];
		/* Set UART device and UART parameters for ModBus using */
	if((command == UMDK_MODBUS_SET_PARAMS) || (command == UMDK_MODBUS_SET_DEVICE)){
		/* Do not allow receiving data*/
		current_pack.rx_allow = UMDK_MODBUS_RX_NOT_ALLOW;
		uart_params_t modbus_params;
		
		uint8_t device = umdk_modbus_config.uart_dev;
		uint32_t baudrate = umdk_modbus_config.baudrate;
		int databits = umdk_modbus_config.databits;
		int stopbits = umdk_modbus_config.stopbits;
		char parity = umdk_modbus_config.parity;
		
		modbus_params.baudrate = umdk_modbus_config.baudrate;
		modbus_params.databits = umdk_modbus_config.databits;
		modbus_params.stopbits = umdk_modbus_config.stopbits;
		modbus_params.parity = umdk_modbus_config.parity;
		
		if(command == UMDK_MODBUS_SET_DEVICE){
			if (cmd->length < 2) { /* Must be one byte of cmd and 1 byte device number */
				puts("[umdk-" _UMDK_NAME_ "] Incorrect device number");
				reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
				return true;
			}
				/* Disable current UART */
			if (uart_init_ext(UART_DEV(umdk_modbus_config.uart_dev), &modbus_params, NULL, NULL)) {
				puts("[umdk-" _UMDK_NAME_ "] Error -> Cannot disable current UART");
				reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
				return true;
			}
			
			umdk_modbus_config.uart_dev = cmd->data[1];
	
			device = umdk_modbus_config.uart_dev;
			databits = umdk_modbus_config.databits + 8;
			
			if(umdk_modbus_config.stopbits == UART_STOPBITS_10) {
				stopbits = 1;
			}
			else if(umdk_modbus_config.stopbits == UART_STOPBITS_20) {
				stopbits = 2;
			}
	
			if(umdk_modbus_config.parity == UART_PARITY_NOPARITY) {
				parity = 'N';
			}
			else if(umdk_modbus_config.parity == UART_PARITY_ODD) {
				parity = 'O';
			}
			else if(umdk_modbus_config.parity == UART_PARITY_EVEN) {
				parity = 'E';
			}
			
		}
		else if(command == UMDK_MODBUS_SET_PARAMS) {
			/* 1 byte command and a string like 115200-8N1 */
			if (cmd->length < 8) { /* Must be one byte of cmd and 8 bytes parameters string */
				puts("[umdk-" _UMDK_NAME_ "] Incorrect data length");
				reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
				return true;
			}

			if (sscanf((char *)&cmd->data[1], "%lu-%d%c%d", &baudrate, &databits, &parity, &stopbits) != 4) {
				puts("[umdk-" _UMDK_NAME_ "] Error parsing parameters");
				reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
				return true;
			}
									
			modbus_params.baudrate = baudrate;
			
			if(databits == 8) {
				modbus_params.databits = UART_DATABITS_8;
			}
			else {
				puts("[umdk-" _UMDK_NAME_ "] invalid number of data bits, must be 8");
				reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
				return true;
			}

			switch (parity) {
				case 'N':
					modbus_params.parity = UART_PARITY_NOPARITY;
					break;
				case 'E':
					modbus_params.parity = UART_PARITY_EVEN;
					break;
				case 'O':
					modbus_params.parity = UART_PARITY_ODD;
					break;
				default:
					puts("[umdk-" _UMDK_NAME_ "] Invalid parity value, must be N, O or E");
					reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
					return true;
			}
				
			switch (stopbits) {
				case 1:
					modbus_params.stopbits = UART_STOPBITS_10;
					break;
				case 2:
					modbus_params.stopbits = UART_STOPBITS_20;
					break;
				default:
					puts("[umdk-" _UMDK_NAME_ "] invalid number of stop bits, must be 1 or 2");
					reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
					return true;
			}
		
		}
		/* Set baudrate and reinitialize UART */
		if (uart_init_ext(UART_DEV(umdk_modbus_config.uart_dev), &modbus_params, umdk_modbus_handler, NULL)) {
			puts("[umdk-" _UMDK_NAME_ "] Error UART -> parameters not supported");
			reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
			return true;
		}
		
		umdk_modbus_config.baudrate = modbus_params.baudrate;
		umdk_modbus_config.databits = modbus_params.databits;
		umdk_modbus_config.parity = modbus_params.parity;
		umdk_modbus_config.stopbits = modbus_params.stopbits;
			
		printf("[umdk-" _UMDK_NAME_ "] Device: %02d Mode: %lu-%u%c%u\n", device, baudrate, databits, parity, stopbits);
		save_config();
		
		if(baudrate > UMDK_MODBUS_BAUDRATE_DEF) {
			time_wait = UMDK_MODBUS_TIMEWAIT_DEF_USEC;
		}
		else {
			time_wait = (uint32_t)((MODBUS_FRAME_BIT * MODBUS_WAIT_FRAME * UMDK_MODBUS_USEC_IN_SEC) / baudrate);
		}
	
		reply_code(reply, UMDK_MODBUS_OK_REPLY);
		return true;
	}
	else if(command <= MODBUS_MAX_CMD){		
		id_device = cmd->data[0];
		if(id_device > MODBUS_MAX_ID) {
			puts("[umdk-" _UMDK_NAME_ "] Invalid ID");
			reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
			return true;
		}
		
		/* Clear buffers of RX/TX data */
		memset(rxbuf, 0, UMDK_MODBUS_BUFF_SIZE);
		memset(txbuf, 0, UMDK_MODBUS_BUFF_SIZE);
		
		current_pack.length_tx = cmd->length;	
		memcpy(txbuf, cmd->data, current_pack.length_tx);
		
		_transmit_pack();
		_check_respose(UMDK_MODBUS_RECIEVE_TIME_MIN_MS, UMDK_MODBUS_TIME_NO_RESPONSE_MS);
		return false;
	}
	
	reply_code(reply, UMDK_MODBUS_ERROR_REPLY);
    return true;
}

#ifdef __cplusplus
}
#endif