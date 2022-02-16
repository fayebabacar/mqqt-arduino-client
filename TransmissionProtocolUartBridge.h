/*
* The MIT License (MIT)
*
* Copyright (C) 2018 Copyright (c) 2018 Gabriel Nikol
*/

#ifndef ARDUINO_MQTTSN_CLIENT_TRANSMISSIONPROTOCOLUARTBRDIGE_H
#define ARDUINO_MQTTSN_CLIENT_TRANSMISSIONPROTOCOLUARTBRDIGE_H

#include "MqttSnMessageHandler.h"
#include "System.h"
#include <cstdint>
#include <cerrno>
#include "global_defines.h"
#include "mqttsn_messages.h"


enum TransmissionProtocolUartBridgeStatus {
    STARTING,
    IDLE,
    SEND,
    RECEIVE,
    CONFIGURATION,
    PARSE_FAILURE,
    ERROR
};

enum SEND_STATUS {
    SEND_NONE,
    AWAIT_ADDRESS,
    AWAIT_DATA,
    SENDING
};

enum RECEIVE_STATUS {
    RECEIVE_NONE,
    SEND_ADDRESS,
    SEND_DATA
};

enum CONFIGURATION_STATUS {
    CONFIGURATION_NONE = 0,
    CONFIGURATION_STATUS = 1,
    CONFIGURATION_OWN_ADDRESS = 2,
    CONFIGURATION_BROADCAST_ADDRESS = 3,
    CONFIGURATION_MAXIMUM_MESSAGE_LENGTH = 4,
    CONFIGURATION_SERIAL_BUFFER_SIZE = 5
};


#define SerialBufferSize 300


template<class TransmissionProtocolUartBridge_SocketInterface>
class TransmissionProtocolUartBridge {
private:
    // SocketInterface
    TransmissionProtocolUartBridge_SocketInterface &socketInterface;
    // friend TransmissionProtocolUartBridge_SocketInterface;

    // Status
    TransmissionProtocolUartBridgeStatus status = STARTING;
    RECEIVE_STATUS receive_status = RECEIVE_NONE;
    SEND_STATUS send_status = SEND_NONE;
    uint8_t configuration_status = CONFIGURATION_NONE;


    // SerialBuffer
    bool lineReady = false;
    char serialBuffer[SerialBufferSize];
    uint16_t serialBufferCounter = 0;

    // SendBuffer
    device_address destination_address;
    uint8_t data[64];
    uint16_t data_length = 0;

    // ReceiveBuffer
    device_address receive_address;
    uint8_t receive_buffer[64];
    uint16_t receive_buffer_length = 0;

public:
    explicit TransmissionProtocolUartBridge(TransmissionProtocolUartBridge_SocketInterface &socketInterface) :
            socketInterface(socketInterface) {}

    bool begin() {
        memset(serialBuffer, 0x0, sizeof(serialBuffer));
        socketInterface.setTransmissionProtocolUartBridge(this);
        if (!socketInterface.begin()) {
            status = ERROR;
            return false;
        }
        status = IDLE;
        return true;
    }

    /*
     * Receive from Serial/UART
     */
    void putChar(char c) {
        serialBuffer[serialBufferCounter++] = c;
        if (c == '\n') {
            lineReady = true;
        } else if (serialBufferCounter == SerialBufferSize) {
            status = PARSE_FAILURE;
        }
    }

    void receiveData(device_address *address, uint8_t *bytes, uint16_t bytes_length) {
        if (bytes[0] != bytes_length) {
            return;
        }
        if (receive_buffer_length == 0 && sizeof(receive_buffer) > bytes[0]) {
            // buffer empty
            // received bytes less or equal to receive_buffer size
            reset_received_buffer();

            memcpy(&receive_address, address, sizeof(device_address));
            receive_buffer_length = bytes[0];
            memcpy(receive_buffer, bytes, receive_buffer_length);
        }
    }

    bool print_received_address() {
        Serial.print("ADDRESS");
        for (uint16_t i = 0; i < sizeof(device_address); i++) {
            Serial.print(" ");
            Serial.print(receive_address.bytes[i], DEC);
        }
        Serial.print("\n");
        return true;
    }

    bool print_received_data() {
        Serial.print("DATA");
        for (uint16_t i = 0; i < receive_buffer_length; i++) {
            Serial.print(" ");
            Serial.print(receive_buffer[i], DEC);
        }
        Serial.print("\n");
        return true;
    }

    void reset_received_buffer() {
        memset(&receive_address, 0x0, sizeof(device_address));
        memset(receive_buffer, 0x0, sizeof(receive_buffer));
        receive_buffer_length = 0;
    }

    void notify_socket_disconnected() {
        status == ERROR;
    }

    void resetChip() {
#if defined(ESP8266)
        Serial.print(F("OK RESET\n"));
        ESP.restart();
#else
        Serial.print(F("ERROR RESET_NOT_SUPPORTED\n"));
#endif
    }

    bool loop() {

        if (!socketInterface.loop()) {
            return false;
        }

        if (lineReady) {
            if (isReset(serialBuffer)) {
                resetSerialBuffer();
                resetChip();
            }
        }

        if (status == STARTING) {
            // begin should be already called
            Serial.print("ERROR NOT_STARTED\n");
            return false;
        }

        if (status == IDLE && lineReady) {
            // parse
            if (isSend(serialBuffer)) {
                status = SEND;
                send_status = SEND_NONE;
                resetSerialBuffer();
            } else if (isReceived(serialBuffer)) {
                status = RECEIVE;
                receive_status = RECEIVE_NONE;
                resetSerialBuffer();
            } else if (isConfiguration(serialBuffer)) {
                status = CONFIGURATION;
                configuration_status = CONFIGURATION_NONE;
                resetSerialBuffer();
            } else if (isReset(serialBuffer)) {
                resetSerialBuffer();
                resetChip();
            } else {
                status = PARSE_FAILURE;
            }
        }


        if (status == SEND) {
            if (send_status == SEND_NONE) {
                Serial.print("OK AWAIT_ADDRESS\n");
                resetSendBuffer();
                send_status = AWAIT_ADDRESS;
            } else if (send_status == AWAIT_ADDRESS && lineReady) {
                if (parseAddress(serialBuffer)) {
                    Serial.print("OK AWAIT_DATA\n");
                    resetSerialBuffer();
                    send_status = AWAIT_DATA;
                } else {
                    status = PARSE_FAILURE;
                }
            } else if (send_status == AWAIT_DATA && lineReady) {
                if (parseData(serialBuffer)) {
                    Serial.print("OK SENDING\n");
                    resetSerialBuffer();
                    send_status = SENDING;
                } else {
                    status = PARSE_FAILURE;
                }
            } else if (send_status == SENDING) {
                if (sendDataToAddress()) {
                    Serial.print("OK IDLE\n");
                    resetSendBuffer();
                    send_status = SEND_NONE;
                    status = IDLE;
                } else {
                    status = ERROR;
                }
            }
        }

        if (status == RECEIVE) {
            if (receive_status == RECEIVE_NONE) {
                Serial.print("OK SEND_ADDRESS\n");
                receive_status = SEND_ADDRESS;
            } else if (receive_status == SEND_ADDRESS) {
                if (send_receive_address()) {
                    Serial.print("OK SEND_DATA\n");
                    receive_status = SEND_DATA;
                } else {
                    status = ERROR;
                }
            } else if (receive_status == SEND_DATA) {
                if (send_receive_data()) {
                    Serial.print("OK IDLE\n");
                    resetReceiveBuffer();
                    receive_status = RECEIVE_NONE;
                    status = IDLE;
                } else {
                    status = ERROR;
                }
            }
        }

        if (status == CONFIGURATION) {
            if (configuration_status == CONFIGURATION_NONE) {
                Serial.print("OK SEND_STATUS\n");
                configuration_status = CONFIGURATION_STATUS;
            } else if (configuration_status == CONFIGURATION_STATUS) {
                if (printStatus()) {
                    Serial.print("OK SEND_OWN_ADDRESS\n");
                    configuration_status = CONFIGURATION_OWN_ADDRESS;
                } else {
                    status = ERROR;
                }
            } else if (configuration_status == CONFIGURATION_OWN_ADDRESS) {
                if (printOwnAddress()) {
                    Serial.print("OK SEND_BROADCAST_ADDRESS\n");
                    configuration_status = CONFIGURATION_BROADCAST_ADDRESS;
                } else {
                    status = ERROR;
                }
            } else if (configuration_status == CONFIGURATION_BROADCAST_ADDRESS) {
                if (printBroadcastAddress()) {
                    Serial.print("OK SEND_MAXIMUM_MESSAGE_LENGTH\n");
                    configuration_status = CONFIGURATION_MAXIMUM_MESSAGE_LENGTH;
                } else {
                    status = ERROR;
                }
            } else if (configuration_status == CONFIGURATION_MAXIMUM_MESSAGE_LENGTH) {
                if (printMaximumMessageLength()) {
                    Serial.print("OK SEND_SERIAL_BUFFER_SIZE\n");
                    configuration_status = CONFIGURATION_SERIAL_BUFFER_SIZE;
                } else {
                    status = ERROR;
                }
            } else if (configuration_status == CONFIGURATION_SERIAL_BUFFER_SIZE) {
                if (printSerialBufferSize()) {
                    Serial.print("OK IDLE\n");
                    configuration_status = CONFIGURATION_NONE;
                    status = IDLE;
                } else {
                    status = ERROR;
                }
            }
        }

        if (status == PARSE_FAILURE) {
            Serial.print("FAILURE PARSE_FAILURE\n");
            resetSerialBuffer();
            receive_status = RECEIVE_NONE;
            send_status = SEND_NONE;
            status = IDLE;
        }
        /*
        if (status == FAILURE) {
            Serial.print("FAILURE\n");
            resetSerialBuffer();
            receive_status = RECEIVE_NONE;
            send_status = SEND_NONE;
            status = STARTING;
        }
        */
        if (status == ERROR) {
            Serial.print("ERROR\n");
            resetSerialBuffer();
            resetChip();
        }

    }

    bool printStatus() {
        Serial.print("STATUS");
        Serial.print(" ");
        if (status == STARTING) {
            Serial.print("STARTING");
        } else if (status == IDLE) {
            Serial.print("IDLE");
        } else if (status == SEND) {
            Serial.print("SEND");
        } else if (status == CONFIGURATION) {
            Serial.print("CONFIGURATION");
        } else if (status == PARSE_FAILURE) {
            Serial.print("PARSE_FAILURE");
        } else if (status == ERROR) {
            Serial.print("ERROR");
        } else {
            Serial.print("\n");
            return false;
        }
        Serial.print("\n");
        return true;
    }

    bool printOwnAddress() {
        Serial.print("OWN_ADDRESS");
        device_address *address = socketInterface.getAddress();
        if (address != nullptr) {
            for (uint16_t i = 0; i < sizeof(device_address); i++) {
                Serial.print(" ");
                Serial.print(address->bytes[i], DEC);
            }
        } else {
            Serial.print(" ");
            Serial.print("N/A");
        }
        Serial.print("\n");
        return true;
    }

    bool printBroadcastAddress() {
        Serial.print("BROADCAST_ADDRESS");
        device_address *address = socketInterface.getBroadcastAddress();
        if (address != nullptr) {
            for (uint16_t i = 0; i < sizeof(device_address); i++) {
                Serial.print(" ");
                Serial.print(address->bytes[i], DEC);
            }
        } else {
            Serial.print(" ");
            Serial.print("N/A");
        }
        Serial.print("\n");
        return true;
    }

    bool printMaximumMessageLength() {
        Serial.print("MAXIMUM_MESSAGE_LENGTH");
        Serial.print(" ");
        uint8_t maximumMessageLength = socketInterface.getMaximumMessageLength();
        Serial.print(maximumMessageLength, DEC);
        Serial.print("\n");
        return true;
    }

    bool printSerialBufferSize() {
        Serial.print("SERIAL_BUFFER_SIZE");
        Serial.print(" ");
        Serial.print(SerialBufferSize, DEC);
        Serial.print("\n");
        return true;
    }


    void printSerialBuffer() {
        if (lineReady) {
            for (uint16_t i = 0; i < serialBufferCounter; i++) {
                Serial.print(serialBuffer[i]);
            }
        } else {
            Serial.print("lineNotReady");
        }
    }

    bool isReset(char *buffer) {
        /*
          char *token = strsep(&buffer, " ");
          if (token == NULL) {
            return false;
          }
        */
        return strncmp(buffer, "RESET", strlen("RESET")) == 0;
        //return memcmp(token, "RESET", strlen("RESET")) == 0;
    }

    void resetReceiveBuffer() {
        reset_received_buffer();
    }

    bool send_error() {
        Serial.print("ERROR\n");
        return true;
    }

    bool send_receive_data() {
        return print_received_data();
    }

    bool send_receive_address() {
        return print_received_address();
    }

    void resetSerialBuffer() {
        memset(serialBuffer, 0x0, sizeof(serialBuffer));
        serialBufferCounter = 0;
        lineReady = false;
    }

    void resetSendBuffer() {
        memset(&destination_address, 0x0, sizeof(device_address));
        memset(data, 0x0, sizeof(data));
        data_length = 0;
    }

    bool sendDataToAddress() {
        return socketInterface.send(&destination_address, (uint8_t * ) & data, data_length);
    }

    bool parseData(char *buffer) {
        char *token = strsep(&buffer, " ");
        if (token == NULL) {
            return false;
        }
        if (memcmp(token, "DATA", strlen("DATA")) != 0) {
            return false;
        }

        memset(data, 0x0, sizeof(data));
        data_length = 0;

        while ((token = strsep(&buffer, " ")) != NULL) {
            long int number = 0;
            if (!parseLong(token, &number)) {
                return false;
            }
            if (number > UINT8_MAX || number < 0) {
                return false;
            }
            data[data_length++] = (uint8_t) number;
        }
        return true;
    }

//#define PARSERADDRESSDEBUG

    bool parseAddress(char *buffer) {
#if defined(PARSERADDRESSDEBUG)
        Serial.print("parseAddress\n");
#endif
        char *token = strsep(&buffer, " ");
        if (token == NULL) {
#if defined(PARSERADDRESSDEBUG)
            Serial.print("token NULL\n");
#endif
            return false;
        }
        if (memcmp(token, "ADDRESS", strlen("ADDRESS")) != 0) {
#if defined(PARSERADDRESSDEBUG)
            Serial.print("does not start with ADDRESS\n");
#endif
            return false;
        }

        memset(&destination_address, 0x0, sizeof(device_address));
        uint16_t destination_address_length = 0;

        while ((token = strsep(&buffer, " ")) != NULL) {
            long int number = 0;
            if (!parseLong(token, &number)) {
#if defined(PARSERADDRESSDEBUG)
                Serial.print("failure parsing parseLong\n");
#endif
                return false;
            }
#if defined(PARSERADDRESSDEBUG)
            Serial.print(number, DEC);
    Serial.print("\n");
#endif
            if (number > UINT8_MAX || number < 0) {
#if defined(PARSERADDRESSDEBUG)
                Serial.print("number out of bounds: ");
      Serial.print(number, DEC);
      Serial.print("\n");
#endif
                return false;
            }
            if (destination_address_length + 1 > sizeof(device_address)) {
#if defined(PARSERADDRESSDEBUG)
                Serial.print("address size too long");
#endif
                return false;
            }
            destination_address.bytes[destination_address_length++] = (uint8_t) number;
        }
        return destination_address_length == sizeof(device_address);
    }

    bool isReceived(char *buffer) {
        char *token = strsep(&buffer, " ");
        if (token == NULL) {
            return false;
        }
        return memcmp(token, "RECEIVE", strlen("RECEIVE")) == 0;
    }

    bool isSend(char *buffer) {
        char *token = strsep(&buffer, " ");
        if (token == NULL) {
            return false;
        }
        return memcmp(token, "SEND", strlen("SEND")) == 0;
    }

    bool isConfiguration(char *buffer) {
        char *token = strsep(&buffer, " ");
        if (token == NULL) {
            return false;
        }
        return memcmp(token, "CONFIGURATION", strlen("CONFIGURATION")) == 0;
    }


    bool parseLong(const char *str, long *val) {
        char *temp;
        bool rc = true;
        errno = 0;
        *val = strtol(str, &temp, 0);

        if (temp == str || (*temp != '\0' && *temp != '\n') ||
            ((*val == LONG_MIN || *val == LONG_MAX) && errno == ERANGE))
            rc = false;

        return rc;
    }


};


#endif //ARDUINO_MQTTSN_CLIENT_TRANSMISSIONPROTOCOLUARTBRDIGE_H

