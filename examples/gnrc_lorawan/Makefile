# name of your application
APPLICATION = gnrc_lorawan

USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += gnrc_netdev_default
USEMODULE += gnrc_lorawan
USEMODULE += gnrc_pktdump
USEMODULE += ps

BOARD ?= b-l072z-lrwan1
RIOTBASE ?= ../../

# Turn on developer helpers
DEVELHELP ?= 1

# use SX1276 by default
DRIVER ?= sx1276

USEMODULE += $(DRIVER)

# Required for the cipher module */
CFLAGS += -DCRYPTO_AES
#
# We can reduce the size of the packet buffer for LoRaWAN, since there's no IP
# support. This will reduce RAM consumption.
CFLAGS += -DGNRC_PKTBUF_SIZE=512


########################### COMPILE TIME CONFIGURATION ########################
# NOTE: The following options can be configured on runtime as well using
# `ifconfig`

# OTAA compile time configuration keys
CFLAGS += -DLORAMAC_APP_KEY_DEFAULT=\{0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\}
CFLAGS += -DLORAMAC_APP_EUI_DEFAULT=\{0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\}
CFLAGS += -DLORAMAC_DEV_EUI_DEFAULT=\{0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\}

# Uncomment and replace with proper keys for joining with ABP
# NOTE: This values will be overriten in case of OTAA.
#CFLAGS += -DLORAMAC_DEV_ADDR_DEFAULT=\{0x00\,0x00\,0x00\,0x00\}
#CFLAGS += -DLORAMAC_NWK_SKEY_DEFAULT=\{0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\}
#CFLAGS += -DLORAMAC_APP_SKEY_DEFAULT=\{0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\,0x00\}

# Comment/uncomment as necessary
CFLAGS += -DLORAMAC_DEFAULT_JOIN_PROCEDURE=LORAMAC_JOIN_OTAA
#CFLAGS += -DLORAMAC_DEFAULT_JOIN_PROCEDURE=LORAMAC_JOIN_ABP

# Uncomment to set the highest DR for the EU868 in order to maximize throughput.
# If uncommented, the default value (DR0) is used.
# Note this value is also used for the OTAA.
#CFLAGS += -DLORAMAC_DEFAULT_DR=LORAMAC_DR_5

# Set the default RX2 datarate to DR3 (used by The Things Network)
CFLAGS += -DLORAMAC_DEFAULT_RX2_DR=LORAMAC_DR_3

# Set default messages to unconfirmable
CFLAGS += -DLORAMAC_DEFAULT_TX_MODE=LORAMAC_TX_CNF

include $(RIOTBASE)/Makefile.include
