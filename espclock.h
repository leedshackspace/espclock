void sendNTPpacket(WiFiUDP *u);
void displayAP();
char displayIP();
void displayDash();
void displayClock();
void clearDigits();
void displayBusy(char digit);
void stopDisplayBusy();
void setupDisplay();
void display();

void setupTime();
time_t getNtpTime();
