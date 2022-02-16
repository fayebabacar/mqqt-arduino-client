/*
* The MIT License (MIT)
*
* Copyright (C) 2018 Copyright (c) 2018 Gabriel Nikol
*/

#include "System.h"

void System::set_heartbeat(uint32_t period) {
    this->heartbeat_period = period;
    this->heartbeat_current = millis();
}

uint32_t System::get_heartbeat() {
    return this->heartbeat_period;
}

bool System::has_beaten() {
    uint32_t current = millis();
    if (current - heartbeat_current > heartbeat_period) {
        this->heartbeat_current = current;
        return true;
    }
    return false;
}

uint32_t System::get_elapsed_time() {
    uint32_t current = millis();
    uint32_t elapsed_time = current - elapsed_current;
    elapsed_current = current;
    return elapsed_time;
}

void System::sleep(uint32_t duration) {
    delay(duration);
}

void System::exit() {
    ESP.restart();
}
