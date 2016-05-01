void sendNTPpacket(WiFiUDP *u);
void displayAP();
char displayIP();
void displayDash();
void displayClock();
void displayBusy(char digit);
void stopDisplayBusy();
void setupDisplay();

void setupTime();
time_t getNtpTime();
