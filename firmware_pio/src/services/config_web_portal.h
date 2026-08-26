#pragma once

namespace ConfigWebPortal {

bool begin(bool keepAlive = false);

void loop();

bool isActive();

bool hasClient();

void stop();

}
